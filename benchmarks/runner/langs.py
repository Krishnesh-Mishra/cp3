#!/usr/bin/env python3
"""Language registry (split from run_suite.py).

Adding a language = add one entry here + its build/run branch.
`run_suite.py` dispatches on language name only.

Health-server rows use per-language best frameworks:
  cpp (Drogon MSVC) / rust (Axum) / go (Gin) / js (Express + Fastify) /
  python (FastAPI/Uvicorn) / java (Spring Boot) / dotnet (ASP.NET/Kestrel) /
  cp3 (std/network). Compute rows use stdlib only + goose where installed.
"""
from __future__ import annotations

import shutil

COMPUTE_LANGUAGES = ["cpp", "rust", "go", "js", "python", "java", "cp3", "goose"]
SERVER_LANGUAGES = ["cpp", "rust", "go", "js", "js-fastify", "python", "java", "dotnet", "cp3", "goose"]
LANGUAGES = COMPUTE_LANGUAGES

GOOSE = "goose"
DOTNET = "dotnet"
JAVA_SPRING = "java-spring"  # java entry builds Spring Boot when deps present

LANG_STYLE = {
    "cpp": "blue", "rust": "orange", "go": "cyan",
    "js": "yellow", "js-fastify": "yellow", "python": "green",
    "java": "red", "java-spring": "red", "dotnet": "blue",
    "cp3": "magenta", "goose": "cyan",
}

GATE_REFERENCES = ("cpp", "rust", "go", "js", "python", "java", "dotnet", "goose")
NATIVE_REFERENCES = ("cpp", "rust")
MULTICORE_TASKS = {"compute-multi", "health-server-multi"}
GATE_BIG_NATIVE = 1.20
GATE_WARN_NATIVE = 1.05
GATE_SCRIPT = 1.00
GATE_MULTI = 1.00
NOISE_FLOOR_MS = 10.0


def have(tool: str) -> bool:
    return shutil.which(tool) is not None


def language_present(language: str, cp3c_exists: bool, server: bool = False) -> bool:
    """True when `language` is runnable on this machine."""
    if language == "cp3":
        return cp3c_exists
    if language == "goose":
        return have("goose")
    if language == "dotnet":
        return have("dotnet")
    if language == "js":
        return have("node")
    if language == "js-fastify":
        return have("node")
    if language == "python":
        return True
    if language == "java":
        return have("java") and have("javac")
    if language == "rust" and server:
        return have("cargo")
    return have({"cpp": "g++", "rust": "rustc", "go": "go"}.get(language, "")) if language in ("cpp", "rust", "go") else False


def language_label(language: str) -> str:
    return {
        "cpp": "C++", "rust": "Rust", "go": "Go", "js": "JavaScript",
        "js-fastify": "JavaScript (Fastify)", "python": "Python",
        "java": "Java", "java-spring": "Java (Spring Boot)",
        "dotnet": "C# (.NET)", "cp3": "CP3", "goose": "Goose",
    }.get(language, language)
