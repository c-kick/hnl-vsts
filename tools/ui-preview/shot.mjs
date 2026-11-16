// Renders SVG files to PNG with headless Chromium: node shot.mjs <playwright index.mjs> <in.svg> <out.png> ...
const [, , pw, ...files] = process.argv;
const { chromium } = await import(pw);
const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 980, height: 560 }, deviceScaleFactor: 2 });
for (let i = 0; i < files.length; i += 2) {
    await page.goto('file://' + files[i]);
    await page.screenshot({ path: files[i + 1] });
}
await browser.close();
