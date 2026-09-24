// Renders math written with pymdownx.arithmatex (generic mode), e.g.
// $a \times b$ inline or $$...$$ as a block, on every page load and after
// each instant navigation.
document$.subscribe(function ({ body }) {
  if (typeof renderMathInElement !== "function") {
    return;
  }
  renderMathInElement(body, {
    delimiters: [
      { left: "$$", right: "$$", display: true },
      { left: "$", right: "$", display: false },
      { left: "\\(", right: "\\)", display: false },
      { left: "\\[", right: "\\]", display: true },
    ],
  });
});
