/* Click-to-maximize for Mermaid diagrams.
 *
 * mkdocs-material renders each ```mermaid``` fence by REPLACING the
 * `<pre class="mermaid"><code>…` block with a host div whose SVG lives in a
 * CLOSED shadow root — unreachable from script. So this works from the source
 * instead: the diagram sources are snapshotted per page before Material's
 * (async) render swaps them out, and a click re-renders the clicked diagram
 * with the same lazily-loaded `mermaid` global into a full-viewport overlay in
 * light DOM, scaled to fit via its viewBox. Click anywhere or press Escape to
 * close. If the source or the global is unavailable (both exist by the time a
 * rendered diagram can be clicked), the host element itself is fullscreened as
 * a fallback — the diagram then shows at its natural size.
 *
 * Replacement happens in place, so document order maps a clicked host back to
 * its snapshotted source. The snapshot comes from a re-fetch of the page's
 * PRISTINE html: Material's component mount strips the `mermaid` class off the
 * live `<pre>`s synchronously — before any extra_javascript runs — so the live
 * DOM never exposes the sources to us; the served document always does (and the
 * fetch is answered from the browser cache). One delegated listener survives
 * Material's instant navigation; `document$` re-fires per page and refreshes
 * the snapshot.
 */
(function () {
  "use strict";

  var OVERLAY_CLASS = "welder-mermaid-max";
  var HOST_MAX_CLASS = "welder-mermaid-host-max";
  var sources = [];
  var renders = 0;

  function snapshot() {
    sources = [];
    fetch(window.location.pathname)
      .then(function (response) {
        return response.text();
      })
      .then(function (html) {
        var doc = new DOMParser().parseFromString(html, "text/html");
        var found = [];
        doc.querySelectorAll("pre.mermaid").forEach(function (pre) {
          found.push(pre.textContent || "");
        });
        sources = found;
      })
      .catch(function () {
        /* keep the fullscreen-host fallback */
      });
  }

  function closeOverlay() {
    var overlay = document.querySelector("." + OVERLAY_CLASS);
    if (overlay) {
      overlay.remove();
    }
    document.querySelectorAll("." + HOST_MAX_CLASS).forEach(function (el) {
      el.classList.remove(HOST_MAX_CLASS);
    });
    document.removeEventListener("keydown", onKeydown);
  }

  function onKeydown(event) {
    if (event.key === "Escape") {
      closeOverlay();
    }
  }

  function overlayElement() {
    var overlay = document.createElement("div");
    overlay.className = OVERLAY_CLASS;
    overlay.setAttribute("role", "dialog");
    overlay.setAttribute(
      "aria-label",
      "Maximized diagram (click or press Escape to close)"
    );
    overlay.addEventListener("click", closeOverlay);
    document.addEventListener("keydown", onKeydown);
    return overlay;
  }

  function openFromSource(source) {
    // Material's loader has already initialized the global (a rendered diagram
    // exists to be clicked), so render a fresh copy into light DOM.
    var overlay = overlayElement();
    document.body.appendChild(overlay);
    mermaid
      .render("__welder_zoom_" + renders++, source)
      .then(function (result) {
        overlay.innerHTML = result.svg;
        var svg = overlay.querySelector("svg");
        if (svg) {
          // Mermaid caps the SVG at its natural width; let it fill the
          // overlay instead (the viewBox keeps the aspect ratio).
          svg.style.maxWidth = "none";
          svg.style.width = "100%";
          svg.style.height = "100%";
          svg.removeAttribute("height");
        }
      })
      .catch(closeOverlay);
  }

  function openFallback(host) {
    // No source / no global: fullscreen the host itself. The closed-shadow SVG
    // then lays out against the viewport and shows at its natural size.
    document.addEventListener("keydown", onKeydown);
    host.classList.add(HOST_MAX_CLASS);
  }

  document.addEventListener("click", function (event) {
    if (!event.target.closest) {
      return;
    }
    if (event.target.closest("." + OVERLAY_CLASS)) {
      return; // the overlay handles its own click
    }
    var maxed = event.target.closest("." + HOST_MAX_CLASS);
    if (maxed) {
      closeOverlay(); // a fullscreened host closes on click
      return;
    }
    var host = event.target.closest(".mermaid");
    if (!host) {
      return;
    }
    var hosts = document.querySelectorAll(".mermaid");
    var index = Array.prototype.indexOf.call(hosts, host);
    var source = index >= 0 ? sources[index] : undefined;
    if (source && typeof mermaid !== "undefined" && mermaid.render) {
      openFromSource(source);
    } else {
      openFallback(host);
    }
  });

  function initPage() {
    closeOverlay();
    snapshot();
  }

  // Material's `document$` re-emits on instant navigation; the sources must be
  // re-snapshotted before that page's diagrams are replaced (the replacement
  // awaits the async mermaid load, so a synchronous subscriber always wins).
  if (typeof document$ !== "undefined" && document$.subscribe) {
    document$.subscribe(initPage);
  } else if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", initPage);
  } else {
    initPage();
  }
})();