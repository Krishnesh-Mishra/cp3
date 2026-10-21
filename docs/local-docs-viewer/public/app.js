// C+++ local docs viewer - client behaviour.
//
// Motion here follows Apple's "Designing Fluid Interfaces": springs with a
// damping ratio + response (not a fixed duration), velocity hand-off from
// gesture to animation, momentum projection for flick decisions, rubber-band
// boundaries, and full interruptibility. See the skill's §4-§6, §9-§10, §14.
(function () {
  "use strict";

  var root = document.documentElement;
  var reduceMotion = window.matchMedia("(prefers-reduced-motion: reduce)");

  /* ------------------------------------------------------------- theming */
  function applyTheme(theme) {
    root.setAttribute("data-theme", theme);
    var light = document.getElementById("hljs-light");
    var dark = document.getElementById("hljs-dark");
    if (light && dark) {
      light.disabled = theme === "dark";
      dark.disabled = theme !== "dark";
    }
    var btn = document.getElementById("theme-toggle");
    if (btn) btn.textContent = theme === "dark" ? "☀" : "◐";
  }

  var saved = localStorage.getItem("cp3-docs-theme");
  var prefersDark = window.matchMedia && window.matchMedia("(prefers-color-scheme: dark)").matches;
  applyTheme(saved || (prefersDark ? "dark" : "light"));

  var themeToggle = document.getElementById("theme-toggle");
  if (themeToggle) {
    themeToggle.addEventListener("click", function () {
      var next = root.getAttribute("data-theme") === "dark" ? "light" : "dark";
      localStorage.setItem("cp3-docs-theme", next);
      applyTheme(next);
    });
  }

  /* ============================================================== springs
     A critically damped spring at damping 1.0 is the house default; a little
     bounce (0.82) is reserved for gestures that carried real momentum, where
     overshoot reads as physical. Response is in seconds and is NOT a duration -
     the settle time emerges from the parameters. */
  function createSpring(opts) {
    var x0 = opts.from;
    var v0 = opts.velocity || 0;
    var to = opts.to;
    var omega0 = (2 * Math.PI) / (opts.response || 0.4);
    var zeta = opts.damping == null ? 1 : opts.damping;
    var raf = 0;
    var startT = 0;
    var done = false;
    var x = x0;
    var v = v0;

    // Exact solution of the damped harmonic oscillator (m = 1). Integrating
    // numerically at 60 fps silently over-damps the motion and erases the
    // overshoot a momentum spring is supposed to have, so we evaluate the
    // closed form instead - it is frame-rate independent and interruption-safe.
    function valueAt(t) {
      var a = x0 - to;
      if (zeta < 1 - 1e-6) {
        var wd = omega0 * Math.sqrt(1 - zeta * zeta);
        var e = Math.exp(-zeta * omega0 * t);
        var B = (v0 + zeta * omega0 * a) / wd;
        return {
          x: to + e * (a * Math.cos(wd * t) + B * Math.sin(wd * t)),
          v: e * ((-zeta * omega0 * a + B * wd) * Math.cos(wd * t) - (zeta * omega0 * B + a * wd) * Math.sin(wd * t)),
        };
      }
      var ec = Math.exp(-omega0 * t);
      var C = v0 + omega0 * a;
      return { x: to + ec * (a + C * t), v: ec * (v0 - omega0 * C * t) };
    }

    function step(now) {
      if (!startT) startT = now;
      var s = valueAt((now - startT) / 1000);
      x = s.x;
      v = s.v;
      if (opts.onUpdate) opts.onUpdate(x, v);
      if (Math.abs(x - to) < 0.3 && Math.abs(v) < 15) {
        if (opts.onUpdate) opts.onUpdate(to, 0);
        done = true;
        if (opts.onComplete) opts.onComplete();
        return;
      }
      raf = requestAnimationFrame(step);
    }
    raf = requestAnimationFrame(step);

    return {
      stop: function () { done = true; cancelAnimationFrame(raf); },
      current: function () { return x; },
      velocity: function () { return v; },
      finished: function () { return done; },
    };
  }

  /** Apple's momentum projection - where a flick is *going* (§6). */
  function project(vel, decel) {
    decel = decel == null ? 0.998 : decel;
    return (vel / 1000) * decel / (1 - decel);
  }

  /** Progressive resistance past an edge instead of a hard stop (§9). */
  function rubberband(overshoot, dimension, constant) {
    constant = constant == null ? 0.55 : constant;
    return (overshoot * dimension * constant) / (dimension + constant * Math.abs(overshoot));
  }

  /* ===================================================== fluid nav drawer
     On mobile the sidebar is a drawer that tracks the finger 1:1, hands its
     release velocity to the spring, and can be grabbed again mid-flight. */
  var sidebar = document.querySelector(".sidebar");
  var scrim = document.querySelector(".scrim");
  var navToggle = document.getElementById("nav-toggle");
  var navClose = document.getElementById("nav-close");
  var isMobile = function () { return window.matchMedia("(max-width: 900px)").matches; };

  var drawerX = 0; // 0 = fully open, -width = fully closed
  var anim = null;
  var grab = null;
  var samples = [];

  function drawerW() {
    if (!sidebar) return 296;
    return sidebar.getBoundingClientRect().width || 296;
  }

  function setX(v) {
    drawerX = v;
    if (sidebar) sidebar.style.transform = "translateX(" + v + "px)";
    if (scrim) {
      var progress = Math.max(0, Math.min(1, 1 - (-v / drawerW())));
      scrim.style.opacity = String(Math.round(progress * 1000) / 1000);
    }
  }

  function stopAnim() {
    if (anim) { anim.stop(); anim = null; }
  }

  function settle(target) {
    if (!scrim) return;
    if (target === 0) {
      document.body.classList.add("nav-open");
    } else {
      document.body.classList.remove("nav-open");
      scrim.hidden = true;
    }
  }

  function animateTo(target, velocity, bounced) {
    stopAnim();
    if (scrim) scrim.hidden = false;

    // Reduced motion: a static transition, no slide or overshoot (§14).
    if (reduceMotion.matches) {
      setX(target);
      settle(target);
      return;
    }
    anim = createSpring({
      from: drawerX,
      to: target,
      velocity: velocity || 0,
      // Bounce only when a flick carried momentum; otherwise crisp.
      damping: bounced ? 0.82 : 1,
      response: 0.32,
      onUpdate: setX,
      onComplete: function () { anim = null; settle(target); },
    });
  }

  function openDrawer() {
    if (!sidebar || !isMobile()) return;
    document.body.classList.add("nav-open");
    if (scrim) scrim.hidden = false;
    setX(drawerX); // start from the live value
    animateTo(0, 0, false);
  }

  function closeDrawer(velocity) {
    if (!sidebar) return;
    animateTo(-drawerW(), velocity || 0, false);
  }

  function syncLayout() {
    if (!sidebar) return;
    if (isMobile()) {
      if (document.body.classList.contains("nav-open")) {
        drawerX = 0;
        sidebar.style.transform = "translateX(0px)";
        if (scrim) { scrim.hidden = false; scrim.style.opacity = "1"; }
      } else {
        drawerX = -drawerW();
        sidebar.style.transform = "translateX(" + drawerX + "px)";
        if (scrim) { scrim.hidden = true; scrim.style.opacity = "0"; }
      }
    } else {
      stopAnim();
      sidebar.style.transform = "";
      document.body.classList.remove("nav-open");
      if (scrim) { scrim.hidden = true; scrim.style.opacity = "0"; }
    }
  }

  syncLayout();

  if (navToggle) {
    navToggle.addEventListener("click", function () {
      if (document.body.classList.contains("nav-open")) closeDrawer(0);
      else openDrawer();
    });
  }
  if (navClose) navClose.addEventListener("click", function () { closeDrawer(0); });
  if (scrim) scrim.addEventListener("click", function () { closeDrawer(0); });

  document.addEventListener("keydown", function (e) {
    if (e.key === "Escape" && document.body.classList.contains("nav-open")) closeDrawer(0);
  });

  document.querySelectorAll(".nav a").forEach(function (a) {
    a.addEventListener("click", function () { closeDrawer(0); });
  });

  if (sidebar) {
    sidebar.addEventListener("pointerdown", function (e) {
      if (!isMobile() || reduceMotion.matches) return;
      if (e.pointerType === "mouse" && e.button !== 0) return;
      stopAnim(); // interrupt mid-flight, animating from the presentation value (§3)
      grab = { id: e.pointerId, startX: e.clientX, x0: drawerX, moved: false };
      samples = [{ t: performance.now(), x: e.clientX }];
    });

    sidebar.addEventListener("pointermove", function (e) {
      if (!grab || e.pointerId !== grab.id) return;
      var dx = e.clientX - grab.startX;
      if (!grab.moved) {
        if (Math.abs(dx) < 10) return; // hysteresis before committing (§10)
        grab.moved = true;
        try { sidebar.setPointerCapture(e.pointerId); } catch (err) {}
        if (scrim) scrim.classList.add("dragging"); // 1:1 tracking, no transition lag
      }
      var w = drawerW();
      var target = grab.x0 + dx;
      if (target > 0) target = rubberband(target, w);              // past fully open
      else if (target < -w) target = -w + rubberband(target + w, w); // past fully closed
      setX(target);

      var now = performance.now();
      samples.push({ t: now, x: e.clientX });
      while (samples.length > 6) samples.shift();
      e.preventDefault();
    });

    function endDrag(e) {
      if (!grab || (e && e.pointerId !== grab.id)) return;
      var g = grab;
      grab = null;
      try { sidebar.releasePointerCapture(g.id); } catch (err) {}
      if (scrim) scrim.classList.remove("dragging");
      if (!g.moved) return; // a tap - let the link click through

      var a = samples[0];
      var b = samples[samples.length - 1];
      var dt = (b.t - a.t) / 1000;
      var vel = dt > 0 ? (b.x - a.x) / dt : 0; // px/s
      var w = drawerW();
      var projected = drawerX + project(vel);

      // Velocity *sign* decides reverse vs. commit; position is the tiebreak (§10).
      var open;
      if (vel < -500) open = false;
      else if (vel > 500) open = true;
      else open = projected > -w * 0.5;

      var momentum = Math.abs(vel) > 650;
      if (open) {
        document.body.classList.add("nav-open");
        if (scrim) scrim.hidden = false;
        animateTo(0, vel, momentum);
      } else {
        animateTo(-w, vel, momentum);
      }
    }
    sidebar.addEventListener("pointerup", endDrag);
    sidebar.addEventListener("pointercancel", endDrag);
  }

  var resizeTimer = null;
  window.addEventListener("resize", function () {
    clearTimeout(resizeTimer);
    resizeTimer = setTimeout(syncLayout, 150);
  });

  // Keep the active page visible in the sidebar.
  var activeLink = document.querySelector(".nav-link.active");
  if (activeLink && activeLink.scrollIntoView) activeLink.scrollIntoView({ block: "center" });

  /* ------------------------------------------------------ search-as-you-type */
  var input = document.getElementById("search-input");
  var results = document.getElementById("search-results");
  var timer = null;
  var current = -1;

  function escapeText(s) {
    var d = document.createElement("div");
    d.textContent = s == null ? "" : s;
    return d.innerHTML;
  }

  function showResults() {
    if (!results) return;
    if (!results.hidden) return;
    results.hidden = false;
    // Materialize, don't just fade: blur/scale in from the field's origin (§12).
    if (!reduceMotion.matches) {
      results.classList.add("sr-enter");
      requestAnimationFrame(function () {
        requestAnimationFrame(function () { results.classList.remove("sr-enter"); });
      });
      // Insurance: rAF is paused in background tabs, so never leave the
      // dropdown stuck at its pre-enter opacity.
      setTimeout(function () { results.classList.remove("sr-enter"); }, 120);
    }
  }

  function hideResults() {
    if (results) results.hidden = true;
    current = -1;
  }

  function renderResults(list, query) {
    if (!results) return;
    if (!query.trim()) return hideResults();
    if (!list.length) {
      results.innerHTML = '<div class="sr-empty">No matches for “' + escapeText(query) + '”</div>';
      return showResults();
    }
    results.innerHTML = list
      .map(function (r) {
        return (
          '<a class="sr-item" href="/view/' + encodeURI(r.rel) + '">' +
          '<div class="sr-title">' + escapeText(r.title) + "</div>" +
          '<div class="sr-path">' + escapeText(r.dir) + " · " + r.hits + " match" + (r.hits === 1 ? "" : "es") + "</div>" +
          "</a>"
        );
      })
      .join("");
    current = -1;
    showResults();
  }

  if (input && results) {
    input.addEventListener("input", function () {
      var q = input.value;
      clearTimeout(timer);
      // Keep latency on the input path low (§1); only debounce the network hop.
      timer = setTimeout(function () {
        if (!q.trim()) return hideResults();
        fetch("/api/search?q=" + encodeURIComponent(q))
          .then(function (r) { return r.json(); })
          .then(function (d) { renderResults(d.results || [], q); })
          .catch(function () { hideResults(); });
      }, 90);
    });

    input.addEventListener("focus", function () {
      if (input.value.trim() && results.innerHTML) showResults();
    });

    input.addEventListener("keydown", function (e) {
      var nodes = [].slice.call(results.querySelectorAll(".sr-item"));
      if (e.key === "ArrowDown") {
        e.preventDefault();
        current = Math.min(current + 1, nodes.length - 1);
      } else if (e.key === "ArrowUp") {
        e.preventDefault();
        current = Math.max(current - 1, 0);
      } else if (e.key === "Enter") {
        if (current >= 0 && nodes[current]) window.location.href = nodes[current].getAttribute("href");
        else window.location.href = "/search?q=" + encodeURIComponent(input.value);
        return;
      } else if (e.key === "Escape") {
        hideResults();
        input.blur();
        return;
      } else {
        return;
      }
      nodes.forEach(function (n, i) { n.classList.toggle("active", i === current); });
      if (nodes[current]) nodes[current].scrollIntoView({ block: "nearest" });
    });

    document.addEventListener("click", function (e) {
      if (!e.target.closest(".search")) hideResults();
    });
  }

  /* ------------------------------------------------------- global shortcuts */
  document.addEventListener("keydown", function (e) {
    var tag = (e.target.tagName || "").toLowerCase();
    if (e.key === "/" && tag !== "input" && tag !== "textarea" && tag !== "select") {
      if (input) {
        e.preventDefault();
        input.focus();
        input.select();
      }
    }
  });

  /* ----------------------------------------------------------- copy code */
  document.querySelectorAll(".code-block").forEach(function (block) {
    var code = block.querySelector("code");
    if (!code) return;
    var btn = document.createElement("button");
    btn.className = "copy-btn";
    btn.type = "button";
    btn.textContent = "Copy";
    btn.addEventListener("click", function () {
      var text = code.innerText;
      var done = function () {
        btn.textContent = "Copied!";
        btn.classList.add("copied");
        setTimeout(function () {
          btn.textContent = "Copy";
          btn.classList.remove("copied");
        }, 1400);
      };
      if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(text).then(done, function () {});
      } else {
        var ta = document.createElement("textarea");
        ta.value = text;
        document.body.appendChild(ta);
        ta.select();
        try { document.execCommand("copy"); done(); } catch (err) {}
        document.body.removeChild(ta);
      }
    });
    block.appendChild(btn);
  });

  /* ------------------------------------------------------------- TOC spy */
  var tocLinks = [].slice.call(document.querySelectorAll(".toc-rail a"));
  if (tocLinks.length && "IntersectionObserver" in window) {
    var byId = {};
    tocLinks.forEach(function (a) {
      byId[decodeURIComponent(a.getAttribute("href").slice(1))] = a.parentElement;
    });
    var observer = new IntersectionObserver(
      function (entries) {
        entries.forEach(function (entry) {
          if (!entry.isIntersecting) return;
          Object.keys(byId).forEach(function (k) { byId[k].classList.remove("active"); });
          var li = byId[entry.target.id];
          if (li) li.classList.add("active");
        });
      },
      { rootMargin: "0px 0px -72% 0px", threshold: 0 },
    );
    Object.keys(byId).forEach(function (id) {
      var el = document.getElementById(id);
      if (el) observer.observe(el);
    });
  }

  /* ---------------------------------------------------------- back to top */
  var toTop = document.getElementById("to-top");
  if (toTop) {
    window.addEventListener("scroll", function () {
      toTop.hidden = window.scrollY < 500;
    }, { passive: true });
    toTop.addEventListener("click", function () {
      window.scrollTo({ top: 0, behavior: reduceMotion.matches ? "auto" : "smooth" });
    });
  }
})();
