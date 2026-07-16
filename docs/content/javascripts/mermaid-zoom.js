/* Click-to-maximize for Mermaid diagrams.
 *
 * mkdocs-material renders each ```mermaid``` fence client-side into a shadow
 * root under a `.mermaid` host element. Clicking a rendered diagram opens a
 * full-viewport overlay holding a clone of its SVG, scaled to fit (the SVG
 * carries a viewBox and its own embedded styles, so the clone is
 * self-contained); click anywhere or press Escape to close.
 *
 * One delegated listener: shadow-DOM click events retarget to the host, so
 * this works regardless of when (or whether) Mermaid has finished rendering,
 * and survives Material's instant navigation.
 */
(function () {
  "use strict";

  var OVERLAY_CLASS = "welder-mermaid-max";

  function closeOverlay() {
    var overlay = document.querySelector("." + OVERLAY_CLASS);
    if (overlay) {
      overlay.remove();
    }
    document.removeEventListener("keydown", onKeydown);
  }

  function onKeydown(event) {
    if (event.key === "Escape") {
      closeOverlay();
    }
  }

  function openOverlay(svg) {
    closeOverlay(); // never stack two

    var clone = svg.cloneNode(true);
    // Mermaid caps the inline SVG at its natural size; let the overlay's CSS
    // scale it to the viewport instead (the viewBox keeps the aspect ratio).
    clone.style.maxWidth = "none";
    clone.style.width = "100%";
    clone.style.height = "100%";
    clone.removeAttribute("height");

    var overlay = document.createElement("div");
    overlay.className = OVERLAY_CLASS;
    overlay.setAttribute("role", "dialog");
    overlay.setAttribute("aria-label", "Maximized diagram (click or press Escape to close)");
    overlay.appendChild(clone);
    overlay.addEventListener("click", closeOverlay);

    document.body.appendChild(overlay);
    document.addEventListener("keydown", onKeydown);
  }

  document.addEventListener("click", function (event) {
    if (event.target.closest && event.target.closest("." + OVERLAY_CLASS)) {
      return; // the overlay handles its own click
    }
    var host = event.target.closest ? event.target.closest(".mermaid") : null;
    if (!host) {
      return;
    }
    var svg =
      (host.shadowRoot && host.shadowRoot.querySelector("svg")) ||
      host.querySelector("svg");
    if (svg) {
      openOverlay(svg);
    }
  });
})();