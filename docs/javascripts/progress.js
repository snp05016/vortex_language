// Reader progress tracker (docs/progress.md).
//
// Records, in this browser only, which pages the reader has loaded and when,
// plus when they open a "??? question" / "??? check" block. Everything lives
// under one localStorage key ("vx-progress"); nothing is sent anywhere. On
// the progress page this also renders a per-section summary and a spaced
// review list.
//
// With navigation.instant on, pages are swapped without a full reload, so
// every initialiser runs from document$.subscribe, which fires on the first
// load and after each instant navigation (see theme.js for the same
// pattern).

(function () {
  var STORAGE_KEY = "vx-progress";

  // Section of the site -> path prefix (site-root relative), in display
  // order. A small fixed list, not the rendered nav: the sidebar only shows
  // the current tab's pages, so it cannot give a full site inventory.
  var SECTIONS = [
    { name: "Learn", prefix: "language-tour/" },
    { name: "Build v0.1", prefix: "compiler/guide/" },
    { name: "Back end", prefix: "backend/" },
    { name: "Optimize", prefix: "optimize/" },
    { name: "GPU", prefix: "gpu/" },
    { name: "MLIR", prefix: "mlir/" },
    { name: "Reference", prefix: "specification/" },
    { name: "Project", prefix: "project/" },
  ];

  // Review page -> the chapter pages it covers. A group is "finished" once
  // every chapter in it has been visited; the finish time is the latest of
  // those visits.
  var REVIEW_GROUPS = [
    {
      review: "language-tour/review-1-4/",
      chapters: [
        "language-tour/01-syntax-philosophy/",
        "language-tour/02-basic-source-rules/",
        "language-tour/03-hello-world/",
        "language-tour/04-variables-and-types/",
      ],
    },
    {
      review: "language-tour/review-5-7/",
      chapters: [
        "language-tour/08-expressions/",
        "language-tour/09-statements/",
        "language-tour/10-declarations/",
      ],
    },
    {
      review: "language-tour/review-8-10/",
      chapters: [
        "language-tour/06-runtime-and-numerical-rules/",
        "language-tour/05-types-planned-for-later/",
        "language-tour/07-kernels-and-parallel-execution/",
      ],
    },
    {
      review: "compiler/guide/review-0-3/",
      chapters: [
        "compiler/guide/stage-0-workbench/",
        "compiler/guide/stage-1-source-and-diagnostics/",
        "compiler/guide/stage-2-lexer/",
        "compiler/guide/stage-3-parser-and-tree/",
      ],
    },
    {
      review: "compiler/guide/review-4-6/",
      chapters: [
        "compiler/guide/stage-4-names-and-scopes/",
        "compiler/guide/stage-5-types-and-rules/",
        "compiler/guide/stage-6-first-machine-code/",
      ],
    },
    {
      review: "compiler/guide/review-7-9/",
      chapters: [
        "compiler/guide/stage-7-functions-and-control-flow/",
        "compiler/guide/stage-8-data-in-memory/",
        "compiler/guide/stage-9-runtime-safety/",
      ],
    },
    {
      review: "compiler/guide/review-10-11/",
      chapters: [
        "compiler/guide/stage-10-matrix-multiplication/",
        "compiler/guide/stage-11-release/",
      ],
    },
  ];

  // The spacing idea: a chapter group is worth reviewing a day after you
  // finish it, then a few days, then a week, then a few weeks. Later
  // thresholds are always also "at least one day", so the gate below only
  // needs the first one.
  var REVIEW_AFTER_DAYS = 1;
  var MS_PER_DAY = 86400000;

  function readState() {
    try {
      var raw = window.localStorage.getItem(STORAGE_KEY);
      var state = raw ? JSON.parse(raw) : {};
      if (!state.pages) {
        state.pages = {};
      }
      if (!state.questions) {
        state.questions = {};
      }
      return state;
    } catch (error) {
      return null; // no storage available (private browsing, blocked, ...)
    }
  }

  function writeState(state) {
    try {
      window.localStorage.setItem(STORAGE_KEY, JSON.stringify(state));
    } catch (error) {
      // No storage available: tracking silently does nothing further.
    }
  }

  function clearState() {
    try {
      window.localStorage.removeItem(STORAGE_KEY);
    } catch (error) {
      // Nothing to clear, or storage is unavailable.
    }
  }

  // The header logo always links to the site root, at whatever depth or
  // subpath the current page is served from ("/" locally, "/vortex_language/"
  // on GitHub Pages), so the base is read from it rather than hard-coded.
  function siteBase() {
    var logo = document.querySelector(
      'a[data-md-component="logo"], a.md-logo'
    );
    try {
      if (logo && logo.href) {
        return new URL(logo.href).pathname.replace(/\/?$/, "/");
      }
    } catch (error) {
      // fall through to the default below
    }
    return "/";
  }

  function currentPath() {
    var base = siteBase();
    var path = window.location.pathname;
    if (path.indexOf(base) === 0) {
      path = path.slice(base.length);
    }
    return path.replace(/^\/+/, "");
  }

  function recordVisit() {
    var state = readState();
    if (!state) {
      return;
    }
    state.pages[currentPath()] = new Date().toISOString();
    writeState(state);
  }

  function recordQuestionOpens() {
    var blocks = document.querySelectorAll("details.question, details.check");
    blocks.forEach(function (details) {
      details.addEventListener("toggle", function () {
        if (!details.open) {
          return;
        }
        var state = readState();
        if (!state) {
          return;
        }
        state.questions[currentPath()] = new Date().toISOString();
        writeState(state);
      });
    });
  }

  function formatDate(iso) {
    return new Date(iso).toLocaleDateString(undefined, {
      year: "numeric",
      month: "short",
      day: "numeric",
    });
  }

  function daysSince(iso) {
    return (Date.now() - new Date(iso).getTime()) / MS_PER_DAY;
  }

  // entries: [{ path, when }] with when an ISO time string. Shared by the
  // per-section lists and the review list: same shape, same look.
  function buildList(entries, base) {
    var list = document.createElement("ul");
    list.className = "vx-progress-list";
    entries.forEach(function (entry) {
      var item = document.createElement("li");

      var link = document.createElement("a");
      link.href = base + entry.path;
      link.textContent = entry.path;
      item.appendChild(link);

      var when = document.createElement("span");
      when.className = "vx-progress-when";
      when.textContent = formatDate(entry.when);
      item.appendChild(when);

      list.appendChild(item);
    });
    return list;
  }

  function renderSections(state, base) {
    var wrap = document.createElement("div");
    wrap.className = "vx-progress-sections";

    SECTIONS.forEach(function (section) {
      var entries = Object.keys(state.pages)
        .filter(function (path) {
          return path.indexOf(section.prefix) === 0;
        })
        .map(function (path) {
          return { path: path, when: state.pages[path] };
        })
        .sort(function (a, b) {
          return a.when < b.when ? 1 : a.when > b.when ? -1 : 0;
        });

      var box = document.createElement("div");
      box.className = "vx-progress-section";

      var heading = document.createElement("h2");
      heading.textContent = section.name;
      box.appendChild(heading);

      var count = document.createElement("p");
      count.className = "vx-progress-count";
      count.textContent =
        entries.length === 0
          ? "No pages visited yet."
          : entries.length === 1
          ? "1 page visited."
          : entries.length + " pages visited.";
      box.appendChild(count);

      if (entries.length > 0) {
        box.appendChild(buildList(entries, base));
      }

      wrap.appendChild(box);
    });

    return wrap;
  }

  function renderReview(state, base) {
    var box = document.createElement("div");
    box.className = "vx-progress-section";

    var heading = document.createElement("h2");
    heading.textContent = "Time to review";
    box.appendChild(heading);

    var due = REVIEW_GROUPS.map(function (group) {
        var finishedAt = null;
        var allVisited = group.chapters.every(function (path) {
          var when = state.pages[path];
          if (!when) {
            return false;
          }
          if (!finishedAt || when > finishedAt) {
            finishedAt = when;
          }
          return true;
        });
        return allVisited ? { path: group.review, when: finishedAt } : null;
      })
      .filter(function (entry) {
        return entry && daysSince(entry.when) >= REVIEW_AFTER_DAYS;
      })
      .sort(function (a, b) {
        return a.when < b.when ? -1 : a.when > b.when ? 1 : 0;
      });

    if (due.length === 0) {
      var empty = document.createElement("p");
      empty.className = "vx-progress-count";
      empty.textContent =
        "Nothing due. A review shows up here once a day has passed since " +
        "you finished the chapters it covers.";
      box.appendChild(empty);
    } else {
      box.appendChild(buildList(due, base));
    }

    return box;
  }

  function render(marker) {
    var state = readState();
    marker.innerHTML = "";

    if (!state) {
      var message = document.createElement("p");
      message.className = "vx-progress-count";
      message.textContent =
        "This browser is not letting the page save progress (private " +
        "browsing or blocked site data), so there is nothing to show.";
      marker.appendChild(message);
      return;
    }

    var base = siteBase();
    marker.appendChild(renderSections(state, base));
    marker.appendChild(renderReview(state, base));

    var button = document.createElement("button");
    button.type = "button";
    button.className = "vx-progress-clear";
    button.textContent = "Clear my progress";
    button.addEventListener("click", function () {
      clearState();
      render(marker);
    });
    marker.appendChild(button);
  }

  function init() {
    recordVisit();
    recordQuestionOpens();

    var marker = document.getElementById("vx-progress");
    if (marker) {
      render(marker);
    }
  }

  if (typeof document$ !== "undefined") {
    document$.subscribe(init);
  } else {
    document.addEventListener("DOMContentLoaded", init);
  }
})();
