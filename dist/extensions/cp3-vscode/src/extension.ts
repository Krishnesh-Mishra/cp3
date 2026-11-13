// dist/extensions/cp3-vscode/src/extension.ts
//
// cp3 language support: activates on the first `.cp3` document and starts the
// compiler's built-in language server as a stdio child process:
//
//     <cp3.compilerPath> lsp --stdio
//
// Failure discipline (PKG-CONTRACTS.md section 3): a missing binary produces
// exactly one clear notification and never a crash loop. The server is started
// again only when `cp3.compilerPath` changes or the user runs the
// `cp3.restartServer` command. All other LSP features come from the server,
// which is being built by the LSP workstream against the same frozen contract.
//
// vscode-languageclient has several independent failure channels (initialize
// failure, connection close, rejected start promise, connection read/write
// errors), each of which can show its own notification, and on a server that
// dies during startup the start promise can even resolve while the connection
// is already gone. This module keeps one `StartAttempt` record per start,
// funnels every channel through it, dedupes the user notifications, and routes
// the library's raw error notifications into the output channel.
//
// The extension also contributes a debugger (`type: "cp3"`). It starts
//
//     <cp3.compilerPath> dap --stdio
//
// which builds the launch program with the Debug profile (codegen emits
// `#line` mapping so gdb sees .cp3 lines), runs it under gdb from PATH, and
// speaks DAP. gdb is deliberately not bundled: a missing gdb fails the launch
// with a clear message.

import * as vscode from "vscode";
import { spawn } from "node:child_process";
import * as path from "node:path";
import {
    CloseAction,
    ErrorAction,
    ErrorHandler,
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    State,
    TransportKind,
} from "vscode-languageclient/node";

import { resolveCompilerPath } from "./compilerPath";
import { registerEditorFeatures } from "./editorFeatures";
import { registerSuggestionEngine } from "./intelligence/suggestionProvider";
import {
    applyPackageJsonSchema,
    registerPackageJsonSchemaCommand,
    workspaceLooksLikeCp3,
} from "./packageJsonSchema";

const LANGUAGE_ID = "cp3";
const CLIENT_ID = "cp3";
const CLIENT_NAME = "cp3 Language Server";
const OUTPUT_CHANNEL_NAME = "cp3 Language Server";
const COMPILER_PATH_SETTING = "cp3.compilerPath";
const DEFAULT_COMPILER_PATH = "cp3c";
const RESTART_COMMAND = "cp3.restartServer";
const TASK_TYPE = "cp3";
const TASK_SOURCE = "cp3";
const PROBLEM_MATCHER = "$cp3c";
const DEBUG_TYPE = "cp3";

const RUN_COMMAND = "cp3.run";
const RUN_OUTPUT_NAME = "cp3 Run";

let outputChannel: vscode.LogOutputChannel | undefined;
let runOutput: vscode.OutputChannel | undefined;
let client: LanguageClient | undefined;

// The start attempt whose client is currently installed. Anything reported by
// an older attempt is stale: it is logged at most and never notifies.
let currentAttempt: StartAttempt | undefined;

// Dedupes notifications per cause. Cleared by the restart command and by any
// `cp3.compilerPath` change, so an explicit retry always talks to the user
// again; automatic retries (there are none) never would.
const notified = new Set<string>();

// One record per `startClient` run. The notification helpers below converge on
// it so that the many failure channels of vscode-languageclient collapse into
// exactly one user-visible message per attempt.
interface StartAttempt {
    readonly command: string;
    reachedRunning: boolean;
    failureReported: boolean;
}

function output(): vscode.LogOutputChannel {
    outputChannel ??= vscode.window.createOutputChannel(OUTPUT_CHANNEL_NAME, { log: true });
    return outputChannel;
}

function log(message: string): void {
    output().appendLine(message);
}

function notifyOnce(key: string, message: string, ...actions: string[]): void {
    if (notified.has(key)) {
        return;
    }
    notified.add(key);
    log(message);
    void vscode.window.showWarningMessage(message, ...actions).then((choice) => {
        if (choice === "Open Settings") {
            void vscode.commands.executeCommand("workbench.action.openSettings", COMPILER_PATH_SETTING);
        } else if (choice === "Retry") {
            void startClient(true);
        }
    });
}

function missingCompilerMessage(setting: string): string {
    return (
        `cp3: language server not found. "${COMPILER_PATH_SETTING}" is "${setting}", ` +
        `which does not resolve to a cp3c executable (a bare name is searched on PATH, ` +
        `also trying cp3c.exe on Windows, then a nearby source checkout's build/bin). ` +
        `Point it at cp3c.exe or install the cp3 toolchain.`
    );
}

/**
 * The one user-visible report for an attempt that died before it was ready.
 * Called from the initialize-failure handler, the state listener, the
 * connection error handler and the `start()` rejection; only the first
 * caller wins, later ones are silent.
 */
function reportStartupFailure(attempt: StartAttempt, detail: string): void {
    if (currentAttempt !== attempt || attempt.reachedRunning || attempt.failureReported) {
        return;
    }
    attempt.failureReported = true;
    log(`cp3: language server failed to start (${detail})`);
    notifyOnce(
        `start-failed:${attempt.command}`,
        `cp3: the language server ("${attempt.command}" lsp --stdio) failed to start. ` +
            `See the "${OUTPUT_CHANNEL_NAME}" output channel for the error, ` +
            `then run "${RESTART_COMMAND}" or click Retry to try again.`,
        "Retry",
        "Open Settings"
    );
}

function errorHandler(attempt: StartAttempt): ErrorHandler {
    return {
        error: (error) => {
            log(`cp3: language server connection error: ${error.message}`);
            if (attempt.reachedRunning && currentAttempt === attempt) {
                notifyOnce(
                    `session-failed:${attempt.command}`,
                    `cp3: language server connection error: ${error.message}. ` +
                        `Run "${RESTART_COMMAND}" or click Retry to start it again.`,
                    "Retry"
                );
            }
            // Never restart automatically: a bad path or a broken server would
            // otherwise turn into a restart loop. `handled: true` keeps the
            // library from showing its own overlapping notification.
            return { action: ErrorAction.Shutdown, handled: true };
        },
        closed: () => {
            log(`cp3: language server connection closed; run "${RESTART_COMMAND}" to start it again.`);
            if (attempt.reachedRunning && currentAttempt === attempt) {
                notifyOnce(
                    `session-failed:${attempt.command}`,
                    `cp3: the language server stopped unexpectedly. ` +
                        `Run "${RESTART_COMMAND}" or click Retry to start it again.`,
                    "Retry"
                );
            }
            return { action: CloseAction.DoNotRestart, handled: true };
        },
    };
}

function runOutputChannel(): vscode.OutputChannel {
    runOutput ??= vscode.window.createOutputChannel(RUN_OUTPUT_NAME);
    return runOutput;
}

// A single `cp3.run` invocation at a time: the editor's top-right button is
// reachable by accident, so starting a second program while one is still
// running is reported instead of silently overlapping output.
let runInFlight = false;

/** Saves the active cp3 document, builds and runs it, streaming output to
 * the `cp3 Run` output channel (no terminal is opened). */
async function runActiveCp3File(): Promise<void> {
    if (runInFlight) {
        void vscode.window.showInformationMessage(
            "cp3: a run is already in progress; see the \"cp3 Run\" output channel."
        );
        return;
    }
    const editor = vscode.window.activeTextEditor;
    if (editor?.document.languageId !== LANGUAGE_ID) {
        void vscode.window.showErrorMessage("cp3: open a .cp3 file to run.");
        return;
    }
    const { document } = editor;
    if (document.isDirty) {
        try {
            await document.save();
        } catch (error) {
            void vscode.window.showErrorMessage(
                `cp3: could not save ${document.fileName}: ${String(error)}`
            );
            return;
        }
    }
    if (document.uri.scheme !== "file") {
        void vscode.window.showErrorMessage("cp3: only files on disk can be run.");
        return;
    }
    const file = document.uri.fsPath;
    const setting = vscode.workspace
        .getConfiguration()
        .get<string>(COMPILER_PATH_SETTING, DEFAULT_COMPILER_PATH);
    const cwd =
        vscode.workspace.getWorkspaceFolder(document.uri)?.uri.fsPath ??
        workspaceCwd() ??
        path.dirname(file);
    const command = resolveCompilerPath(setting, cwd);
    if (command === undefined) {
        void vscode.window.showErrorMessage(missingCompilerMessage(setting), "Open Settings").then((choice) => {
            if (choice === "Open Settings") {
                void vscode.commands.executeCommand(
                    "workbench.action.openSettings",
                    COMPILER_PATH_SETTING
                );
            }
        });
        return;
    }
    const output = runOutputChannel();
    output.clear();
    output.appendLine(`> "${command}" run "${file}"`);
    output.appendLine("");
    // Show the channel but keep keyboard focus in the editor.
    output.show(true);
    runInFlight = true;
    output.appendLine(`(cwd: ${cwd})`);
    output.appendLine("");
    const child = spawn(command, ["run", file], { cwd, windowsHide: true });
    child.stdout?.setEncoding("utf8");
    child.stdout?.on("data", (chunk: string) => output.append(chunk));
    child.stderr?.setEncoding("utf8");
    child.stderr?.on("data", (chunk: string) => output.append(chunk));
    child.on("error", (error) => {
        output.appendLine(`cp3: failed to start: ${error.message}`);
        void vscode.window.showErrorMessage(
            `cp3: failed to start (${command}): ${error.message}`
        );
        runInFlight = false;
    });
    child.on("close", (code, signal) => {
        runInFlight = false;
        const summary =
            code !== null
                ? `--- exit code ${String(code)} ---`
                : `--- terminated by signal ${String(signal)} ---`;
        output.appendLine("");
        output.appendLine(summary);
        if (code !== null && code !== 0) {
            void vscode.window.showErrorMessage(
                `cp3: run failed (exit code ${String(code)}).`,
                "Show Output"
            ).then((choice) => {
                if (choice === "Show Output") {
                    runOutputChannel().show();
                }
            });
        }
    });
}

function workspaceCwd(): string | undefined {
    return vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
}

interface Cp3TaskSpec {
    readonly name: string;
    readonly task: "build" | "run" | "check";
    readonly group?: vscode.TaskGroup;
}

// The three compiler tasks contributed to `Tasks: Run Task`. Each runs
// `<cp3.compilerPath> <task> ${file}` in the first workspace folder and pipes
// the compiler's machine lines through the `$cp3c` problem matcher, so build,
// run and check report into the Problems panel exactly like `cp3c check`.
const CP3_TASKS: readonly Cp3TaskSpec[] = [
    { name: "cp3: build", task: "build", group: vscode.TaskGroup.Build },
    { name: "cp3: run", task: "run" },
    { name: "cp3: check", task: "check", group: vscode.TaskGroup.Test },
];

function taskCompilerCommand(): string {
    const setting = vscode.workspace
        .getConfiguration()
        .get<string>(COMPILER_PATH_SETTING, DEFAULT_COMPILER_PATH);
    // An unresolved bare name is still handed to the process unchanged: the
    // task terminal then shows the usual "not recognized" error instead of the
    // task silently disappearing from the list.
    return resolveCompilerPath(setting, workspaceCwd()) ?? setting;
}

// Builds the contributed tasks and re-resolves tasks saved in tasks.json, so
// both pick up the current `cp3.compilerPath` and workspace folder.
class Cp3TaskProvider implements vscode.TaskProvider {
    provideTasks(): vscode.Task[] {
        return CP3_TASKS.map((spec) => this.buildTask(spec));
    }

    resolveTask(task: vscode.Task): vscode.Task | undefined {
        const wanted = task.definition.task;
        const spec = CP3_TASKS.find((entry) => entry.task === wanted);
        return spec === undefined ? undefined : this.buildTask(spec);
    }

    private buildTask(spec: Cp3TaskSpec): vscode.Task {
        const folder = vscode.workspace.workspaceFolders?.[0];
        const definition: vscode.TaskDefinition = {
            type: TASK_TYPE,
            task: spec.task,
        };
        const execution = new vscode.ProcessExecution(
            taskCompilerCommand(),
            [spec.task, "${file}"],
            { cwd: folder?.uri.fsPath }
        );
        const task = new vscode.Task(
            definition,
            folder ?? vscode.TaskScope.Workspace,
            spec.name,
            TASK_SOURCE,
            execution,
            [PROBLEM_MATCHER]
        );
        if (spec.group !== undefined) {
            task.group = spec.group;
        }
        task.presentationOptions = {
            reveal: vscode.TaskRevealKind.Always,
            panel: vscode.TaskPanelKind.Dedicated,
            clear: true,
        };
        return task;
    }
}

async function stopClient(): Promise<void> {
    const running = client;
    client = undefined;
    currentAttempt = undefined;
    if (running === undefined) {
        return;
    }
    try {
        await running.stop();
    } catch (error) {
        // The server may already be gone (crash, missing binary); stopping a
        // dead client is not an error worth surfacing.
        log(`cp3: stop() failed, ignoring: ${String(error)}`);
    }
}

/**
 * Starts (or restarts) the language client. `forced` clears the notification
 * dedupe, so the user-triggered paths (restart command, Retry button) always
 * report a still-broken configuration.
 */
export async function startClient(forced = false): Promise<void> {
    if (forced) {
        notified.clear();
    }
    await stopClient();

    const setting = vscode.workspace
        .getConfiguration()
        .get<string>(COMPILER_PATH_SETTING, DEFAULT_COMPILER_PATH);
    const command = resolveCompilerPath(setting, workspaceCwd());
    if (command === undefined) {
        notifyOnce(
            `missing:${setting}`,
            missingCompilerMessage(setting),
            "Open Settings",
            "Retry"
        );
        return;
    }

    const attempt: StartAttempt = { command, reachedRunning: false, failureReported: false };
    const clientOptions: LanguageClientOptions = {
        documentSelector: [
            { scheme: "file", language: LANGUAGE_ID },
            { scheme: "untitled", language: LANGUAGE_ID },
        ],
        outputChannel: output(),
        errorHandler: errorHandler(attempt),
        initializationFailedHandler: (error) => {
            // Returning true would make the library re-send `initialize` to
            // the same broken server. Report once and stay down instead.
            reportStartupFailure(attempt, error instanceof Error ? error.message : String(error));
            return false;
        },
    };
    const serverOptions: ServerOptions = {
        command,
        // vscode-languageclient appends `--stdio` itself for
        // TransportKind.stdio, so the child process gets exactly the
        // contract's `<cp3c> lsp --stdio`.
        args: ["lsp"],
        transport: TransportKind.stdio,
        options: { cwd: workspaceCwd() },
    };

    const started = new LanguageClient(CLIENT_ID, CLIENT_NAME, serverOptions, clientOptions);
    client = started;
    currentAttempt = attempt;

    // The library raises raw error notifications of its own on some startup
    // failure paths (for example when the connection closes while the
    // initialize response is still pending, or when the start promise settles
    // through the close handler). The extension owns the user-facing messages,
    // so route them into the output channel only.
    const libraryError = started.error.bind(started);
    started.error = (message, data) => {
        libraryError(message, data, false);
    };

    started.onDidChangeState((event) => {
        if (event.newState === State.Running) {
            if (currentAttempt === attempt) {
                attempt.reachedRunning = true;
                log("cp3: language server started");
            } else {
                // A replaced attempt finished connecting late; stop it so the
                // already-running server process does not linger.
                log("cp3: stale language server start completed; stopping it");
                void started.stop();
            }
        } else if (event.newState === State.Stopped) {
            if (attempt.reachedRunning) {
                log("cp3: language server stopped");
            } else {
                if (client === started) {
                    client = undefined;
                }
                reportStartupFailure(attempt, "the server exited before it was ready");
            }
        }
    });

    log(`cp3: starting "${command} lsp --stdio"`);
    try {
        await started.start();
    } catch (error) {
        // Usually the state listener or the initialize handler reported first;
        // this is the fallback for a plain rejected start promise.
        if (client === started) {
            client = undefined;
        }
        reportStartupFailure(attempt, error instanceof Error ? error.message : String(error));
    }
}

/**
 * Fills the launch attributes the adapter needs. `program` defaults to the
 * active .cp3 file (so "cp3: Debug current file" works) and `cwd` to the
 * workspace folder. Returning undefined cancels a launch that has no program.
 */
class Cp3DebugConfigurationProvider implements vscode.DebugConfigurationProvider {
    resolveDebugConfiguration(
        folder: vscode.WorkspaceFolder | undefined,
        config: vscode.DebugConfiguration
    ): vscode.DebugConfiguration | undefined {
        // An empty configuration means "show the launch.json picker"; VS Code
        // calls this provider with nothing set in that case.
        if (!config.type && !config.request && !config.name) {
            return undefined;
        }
        if (!config.program) {
            const editor = vscode.window.activeTextEditor;
            if (editor?.document.languageId !== LANGUAGE_ID) {
                void vscode.window.showErrorMessage(
                    "cp3: set \"program\" to a .cp3 file in launch.json, or open one in the editor."
                );
                return undefined;
            }
            config.program = editor.document.uri.fsPath;
        }
        config.cwd ??= folder?.uri.fsPath ?? workspaceCwd();
        return config;
    }
}

/** Starts `<cp3.compilerPath> dap --stdio` for `type: "cp3"` sessions. */
class Cp3DebugAdapterFactory implements vscode.DebugAdapterDescriptorFactory {
    createDebugAdapterDescriptor(
        session: vscode.DebugSession
    ): vscode.ProviderResult<vscode.DebugAdapterDescriptor> {
        const setting = vscode.workspace
            .getConfiguration()
            .get<string>(COMPILER_PATH_SETTING, DEFAULT_COMPILER_PATH);
        const command = resolveCompilerPath(
            setting,
            session.workspaceFolder?.uri.fsPath ?? workspaceCwd()
        );
        if (command === undefined) {
            throw new Error(missingCompilerMessage(setting));
        }
        return new vscode.DebugAdapterExecutable(command, ["dap", "--stdio"], {
            cwd: session.workspaceFolder?.uri.fsPath ?? workspaceCwd(),
        });
    }
}

export async function activate(context: vscode.ExtensionContext): Promise<void> {
    context.subscriptions.push(output());
    context.subscriptions.push(runOutputChannel());
    registerEditorFeatures(context);
    registerSuggestionEngine(context);
    context.subscriptions.push(
        vscode.commands.registerCommand(RESTART_COMMAND, () => startClient(true))
    );
    context.subscriptions.push(
        vscode.commands.registerCommand(RUN_COMMAND, () => runActiveCp3File())
    );
    context.subscriptions.push(
        vscode.tasks.registerTaskProvider(TASK_TYPE, new Cp3TaskProvider())
    );
    context.subscriptions.push(
        vscode.debug.registerDebugConfigurationProvider(
            DEBUG_TYPE,
            new Cp3DebugConfigurationProvider()
        )
    );
    context.subscriptions.push(
        vscode.debug.registerDebugAdapterDescriptorFactory(
            DEBUG_TYPE,
            new Cp3DebugAdapterFactory()
        )
    );
    registerPackageJsonSchemaCommand(context);
    // A cp3 project's package.json uses the cp3 manifest schema (not npm's);
    // associate it before the first JSON file is validated. The language
    // server only auto-starts for cp3-looking workspaces, so opening an npm
    // project's package.json does not spawn cp3c; opening a .cp3 file still
    // starts it here.
    const looksLikeCp3 = await workspaceLooksLikeCp3().catch(() => false);
    if (looksLikeCp3) {
        void applyPackageJsonSchema(context, false).catch(() => undefined);
        await startClient();
    } else {
        let serverStarted = false;
        context.subscriptions.push(
            vscode.workspace.onDidOpenTextDocument((document) => {
                if (document.languageId === LANGUAGE_ID && !serverStarted) {
                    serverStarted = true;
                    void startClient();
                }
            })
        );
        // The document that triggered this activation (onLanguage:cp3) has
        // already fired `onDidOpenTextDocument` by the time we get here, so
        // the listener above would never see it. Start the server inline for
        // any cp3 document that is already open.
        for (const document of vscode.workspace.textDocuments) {
            if (document.languageId === LANGUAGE_ID && !serverStarted) {
                serverStarted = true;
                void startClient();
            }
        }
    }
    context.subscriptions.push(
        vscode.workspace.onDidChangeConfiguration((event) => {
            if (event.affectsConfiguration(COMPILER_PATH_SETTING)) {
                void startClient(true);
            }
        })
    );
}

export function deactivate(): Thenable<void> | undefined {
    const running = client;
    client = undefined;
    currentAttempt = undefined;
    return running?.stop();
}
