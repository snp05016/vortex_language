// Tick persistence for the workbook page (docs/compiler/workbook.md).
//
// The page lists task-list checkboxes under "## <stage>" headings. This
// script remembers which ones are checked, in this browser only: the key is
// the enclosing heading's id plus the item's text, so a tick survives a
// reload and an instant navigation but never leaves localStorage.
//
// With navigation.instant on, pages are swapped without a full reload, so
// init runs from document$.subscribe, which fires on the first load and
// after each instant navigation (see theme.js for the same pattern).

(function () {
  var PREFIX = "vx-workbook:";

  function readTicked(key) {
    try {
      return window.localStorage.getItem(PREFIX + key) === "1";
    } catch (error) {
      return false;
    }
  }

  function writeTicked(key, ticked) {
    try {
      if (ticked) {
        window.localStorage.setItem(PREFIX + key, "1");
      } else {
        window.localStorage.removeItem(PREFIX + key);
      }
    } catch (error) {
      // No storage available (private browsing, blocked site data, ...):
      // the checkbox still works, it just will not be remembered.
    }
  }

  function clearTicked(keys) {
    try {
      keys.forEach(function (key) {
        window.localStorage.removeItem(PREFIX + key);
      });
    } catch (error) {
      // Nothing to clear, or storage is unavailable.
    }
  }

  // The key must survive re-rendering, so it is built from the nearest
  // heading id (stable: one per stage section) and the item's own text
  // (stable: it is the task itself), not from list position.
  function keyFor(headingId, checkbox) {
    var item = checkbox.closest("li");
    var text = item ? item.textContent.replace(/\s+/g, " ").trim() : "";
    return headingId + " :: " + text;
  }

  function init() {
    var marker = document.getElementById("vx-workbook");
    if (!marker) {
      return;
    }

    var root = marker.closest("article") || document.body;
    var nodes = root.querySelectorAll(
      "h1[id], h2[id], h3[id], h4[id], h5[id], h6[id], li.task-list-item input[type=checkbox]"
    );

    var headingId = "";
    var keys = [];
    var checkboxes = [];

    nodes.forEach(function (node) {
      if (/^H[1-6]$/.test(node.tagName)) {
        headingId = node.id;
        return;
      }
      var key = keyFor(headingId, node);
      keys.push(key);
      checkboxes.push(node);
      node.checked = readTicked(key);
      node.addEventListener("change", function () {
        writeTicked(key, node.checked);
      });
    });

    var next = marker.nextElementSibling;
    if (next && next.classList.contains("vx-workbook-clear")) {
      return;
    }

    var button = document.createElement("button");
    button.type = "button";
    button.className = "vx-workbook-clear";
    button.textContent = "Clear my ticks";
    button.addEventListener("click", function () {
      clearTicked(keys);
      checkboxes.forEach(function (checkbox) {
        checkbox.checked = false;
      });
    });
    marker.insertAdjacentElement("afterend", button);
  }

  if (typeof document$ !== "undefined") {
    document$.subscribe(init);
  } else {
    document.addEventListener("DOMContentLoaded", init);
  }
})();
