// Page enhancements for the Vortex docs (Zensical classic / Material).
//
// With navigation.instant on, pages are swapped without a full reload, so
// every initialiser runs from document$.subscribe, which fires on the first
// load and after each instant navigation.

(function () {
  const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)");

  // Step-through panels. Without JavaScript every .vx-step stays visible;
  // with it, readers step through one panel at a time.
  function enhanceSteppers(root) {
    root.querySelectorAll(".vx-stepper").forEach(function (stepper) {
      const steps = stepper.querySelectorAll(".vx-step");
      if (steps.length < 2 || stepper.classList.contains("is-enhanced")) {
        return;
      }
      let current = 0;
      const controls = document.createElement("div");
      controls.className = "vx-stepper-controls";
      const back = document.createElement("button");
      back.type = "button";
      back.textContent = "Back";
      const next = document.createElement("button");
      next.type = "button";
      next.textContent = "Next step";
      const status = document.createElement("span");
      status.className = "vx-stepper-status";
      status.setAttribute("aria-live", "polite");
      controls.append(back, next, status);
      stepper.appendChild(controls);
      stepper.classList.add("is-enhanced");

      function show(index) {
        current = index;
        steps.forEach(function (step, i) {
          step.classList.toggle("is-current", i === current);
        });
        back.disabled = current === 0;
        next.disabled = current === steps.length - 1;
        status.textContent = `Step ${current + 1} of ${steps.length}`;
      }

      back.addEventListener("click", function () {
        show(Math.max(0, current - 1));
      });
      next.addEventListener("click", function () {
        show(Math.min(steps.length - 1, current + 1));
      });
      show(0);
    });
  }

  // Every animated figure gets a pause/play button (WCAG 2.2.2). CSS
  // animations pause through the .is-paused class; SVG <animate> elements
  // through pauseAnimations(). Readers who prefer reduced motion start paused.
  const animatedSelector = ".vx-flow, .vx-seq, .vx-travel, .vx-pulse";

  function isAnimated(figure) {
    return (
      figure.querySelector(animatedSelector) !== null ||
      figure.querySelector("animate, animateMotion, animateTransform") !== null
    );
  }

  function setPaused(figure, button, paused) {
    figure.classList.toggle("is-paused", paused);
    figure.querySelectorAll("svg").forEach(function (svg) {
      if (paused && svg.pauseAnimations) {
        svg.pauseAnimations();
      } else if (!paused && svg.unpauseAnimations) {
        svg.unpauseAnimations();
      }
    });
    button.textContent = paused ? "Play animation" : "Pause animation";
    button.setAttribute("aria-pressed", paused ? "true" : "false");
  }

  function addAnimationControls(root) {
    root.querySelectorAll(".vx-figure").forEach(function (figure) {
      if (!isAnimated(figure) || figure.querySelector(".vx-anim-toggle")) {
        return;
      }
      const button = document.createElement("button");
      button.type = "button";
      button.className = "vx-anim-toggle";
      button.addEventListener("click", function () {
        setPaused(figure, button, !figure.classList.contains("is-paused"));
      });
      figure.insertBefore(button, figure.firstChild);
      setPaused(figure, button, reducedMotion.matches);
      if (reducedMotion.matches) {
        figure.querySelectorAll("svg").forEach(function (svg) {
          if (svg.setCurrentTime) {
            svg.setCurrentTime(0);
          }
        });
      }
    });
  }

  function init() {
    enhanceSteppers(document);
    addAnimationControls(document);
  }

  if (typeof document$ !== "undefined") {
    document$.subscribe(init);
  } else {
    document.addEventListener("DOMContentLoaded", init);
  }
})();
