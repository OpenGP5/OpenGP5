// OpenGP5: Docsy's Mermaid plugin (0.18.1) without its dark-mode theme swap: OpenGP5's diagrams are dark panels in
// both modes (hugo.yaml sets their colours), so a change of mode needs no new drawing.
// Mermaid plugin entry: imports the pinned library named in the companion's
// config block and renders the page's `.mermaid` blocks. Design:
// https://www.docsy.dev/project/design/script-loading/#ordering-decisions
(async function () {
  'use strict';

  if (!document.querySelector('.mermaid')) return;

  const block = document.querySelector(
    'script[type="application/json"][data-docsy-plugin="mermaid"]',
  );
  if (!block) return;

  try {
    const config = JSON.parse(block.textContent);
    const { default: mermaid } = await import(config.url);

    const settings = { ...(config.options ?? {}), startOnLoad: false };
    mermaid.initialize(settings);

    // Wait for `load`: fonts loaded through CSS are in by then, so label
    // geometry matches the pre-plugin, load-bound auto-start.
    if (document.readyState !== 'complete') {
      await new Promise((resolve) =>
        window.addEventListener('load', resolve, { once: true }),
      );
    }
    await mermaid.run();
  } catch (err) {
    console.error('Mermaid failed to render', err);
  }
})();
