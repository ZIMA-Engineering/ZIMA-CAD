// Render local factory references; no network assets or browser user profile are used.
const { chromium } = require('playwright');
const fs = require('node:fs/promises');
const path = require('node:path');
const { pathToFileURL } = require('node:url');

async function main() {
  const repository = path.resolve(__dirname, '..');
  const dataOption = process.argv.indexOf('--data');
  const source = dataOption >= 0 ? path.resolve(process.argv[dataOption + 1]) : path.join(__dirname, 'data/unit-tables.json');
  const data = JSON.parse(await fs.readFile(source, 'utf8'));
  const root = path.join(repository, data.root || 'config/lib/03-ENGINEERING-TABLES/01-GENERAL-STANDARDS/01-UNITS');
  const evidence = path.join(repository, 'build/reference-table-verification', path.basename(source, '.json'));
  await fs.mkdir(evidence, { recursive: true });
  const index = process.argv.indexOf('--browser');
  const browser = await chromium.launch({ headless: true,
    ...(index >= 0 ? { executablePath: process.argv[index + 1] } : {}) });
  const context = await browser.newContext({ viewport: { width: 1100, height: 760 }, colorScheme: 'light' });
  const failures = [], results = [], errors = [];
  await context.route(/^https?:/, route => route.abort());
  try {
    const topicOption = process.argv.indexOf('--topic');
    const prefixOption = process.argv.indexOf('--topic-prefix');
    const topics = data.pages.filter(topic => topic.rows.length &&
      (!process.argv.includes('--only-frozen') || topic.freeze_header) &&
      (topicOption < 0 || topic.path === process.argv[topicOption + 1]) &&
      (prefixOption < 0 || topic.path.startsWith(process.argv[prefixOption + 1])));
    if (!topics.length) throw Error('No reference topics selected');
    let next = 0;
    const worker = async () => {
      const page = await context.newPage();
      page.on('pageerror', error => errors.push(error.message));
      page.on('requestfailed', request => errors.push(request.url() + ': ' + request.failure().errorText));
      try { while (next < topics.length) {
      const topic = topics[next++];
      const folder = path.join(root, topic.path, '0000-index');
      for (const language of data.languages) {
        const url = pathToFileURL(path.join(folder, `index_${language}.html`)).href;
        await page.goto(url, { waitUntil: 'load' });
        await page.evaluate(() => document.fonts.ready);
        const metrics = await page.evaluate(() => {
          const main = document.querySelector('main').getBoundingClientRect();
          const table = document.querySelector('.table-scroll');
          return { height: Math.ceil(main.height), width: document.documentElement.scrollWidth,
            tableWidth: table?.scrollWidth || 0, tableAvailable: table?.clientWidth || 0,
            language: document.documentElement.lang, rows: [...document.querySelectorAll('tbody tr')].filter(row => !row.closest('.print-only')).length };
        });
        // References may scroll vertically. Width and readable content still
        // have to fit; thumbnails show the schematic and beginning of the table.
        if (metrics.width > 1100 || (!topic.freeze_header && metrics.tableWidth > metrics.tableAvailable))
          failures.push({ topic: topic.path, language, ...metrics });
        const expectedRows = topic.rows.length + (topic.sections || []).reduce((n, block) => n + block.rows.length, 0);
        if (metrics.language !== language || metrics.rows !== expectedRows)
          throw Error(`Rendered content mismatch: ${topic.path}/${language}`);
        if (topic.freeze_header) {
          const frozen = await page.evaluate(() => [...document.querySelectorAll('.table-scroll--frozen')].map(region => {
            const originalWidth = region.style.width;
            region.style.width = '240px';
            const header = region.querySelector('thead th');
            const first = region.querySelector('tbody th');
            const headerTop = header.getBoundingClientRect().top;
            const firstLeft = first.getBoundingClientRect().left;
            region.scrollTop = 220; region.scrollLeft = 160;
            const result = { scrollTop: region.scrollTop, scrollLeft: region.scrollLeft,
              needsVerticalScroll: region.scrollHeight > region.clientHeight,
              needsHorizontalScroll: region.scrollWidth > region.clientWidth,
              headerShift: header.getBoundingClientRect().top - headerTop,
              columnShift: first.getBoundingClientRect().left - firstLeft };
            region.scrollTop = 0; region.scrollLeft = 0;
            region.style.width = originalWidth;
            return result;
          }));
          if (frozen.some(item => (item.needsVerticalScroll && item.scrollTop <= 0) || (item.needsHorizontalScroll && item.scrollLeft <= 0) ||
            Math.abs(item.headerShift) > 1 || Math.abs(item.columnShift) > 1))
            throw Error(`Frozen reference headers moved: ${JSON.stringify(frozen)}`);
          metrics.frozen = frozen;
        }
        const bounds = await page.locator('main').boundingBox();
        const image = await page.screenshot({ type: 'jpeg', quality: 85,
          clip: { x: bounds.x, y: bounds.y, width: bounds.width, height: Math.min(bounds.height, 760) } });
        await fs.writeFile(path.join(folder, `01_${language}.jpg`), image);
        if (language === 'cs') await fs.writeFile(path.join(folder, '01.jpg'), image);
        const pdfPath = path.join(evidence, (topic.path || 'root').replaceAll('/', '_') + `_${language}.pdf`);
        await page.pdf({ path: pdfPath, format: 'A4', preferCSSPageSize: true, printBackground: true });
        await page.emulateMedia({ colorScheme: 'dark' });
        const contrast = await page.evaluate(() => {
          const rgb = value => value.match(/\d+/g).slice(0, 3).map(Number);
          const luminance = values => values.map(v => v / 255).map(v => v <= .04045 ? v / 12.92 : ((v + .055) / 1.055) ** 2.4)
            .reduce((sum, value, i) => sum + value * [.2126, .7152, .0722][i], 0);
          const style = getComputedStyle(document.body);
          const a = luminance(rgb(style.color)), b = luminance(rgb(style.backgroundColor));
          return (Math.max(a, b) + .05) / (Math.min(a, b) + .05);
        });
        if (contrast < 4.5) throw Error(`Insufficient dark contrast: ${topic.path}/${language}`);
        if (language === 'cs' && ['', '01-BASE-UNITS', '04-SI-PREFIXES', '06-IMPERIAL-US/02-VOLUME'].includes(topic.path))
          await page.screenshot({ path: path.join(evidence, (topic.path || 'root').replaceAll('/', '_') + '-dark.png'), fullPage: true });
        await page.emulateMedia({ colorScheme: 'light' });
        results.push({ topic: topic.path, language, ...metrics, darkContrast: contrast });
        if (results.length % 100 === 0) console.log(JSON.stringify({ rendered: results.length, expected: topics.length * data.languages.length }));
      }
      }} finally { await page.close(); }
    };
    // Independent page/media state; bounded concurrency avoids serial browser waits.
    await Promise.all(Array.from({ length: Math.min(3, topics.length) }, worker));
    // Group folders use a real descendant table as their preview, without HTML landing pages.
    for (const topic of data.pages.filter(topic => !topic.rows.length)) {
      const descendant = data.pages.find(child => child.rows.length &&
        (!topic.path || child.path.startsWith(topic.path + '/')));
      // Some source categories intentionally await a reliable source. They
      // retain native navigation; do not fabricate a preview from another topic.
      if (!descendant) continue;
      if ((topicOption >= 0 || prefixOption >= 0) && !topics.includes(descendant)) continue;
      const folder = path.join(root, topic.path, '0000-index');
      for (const language of [...data.languages, '']) {
        const name = language ? `01_${language}.jpg` : '01.jpg';
        await fs.copyFile(path.join(root, descendant.path, '0000-index', name), path.join(folder, name));
      }
    }
    const report = { viewport: '1100 x 760 CSS pixels', results, failures, errors };
    await fs.writeFile(path.join(evidence, 'layout.json'), JSON.stringify(report, null, 2) + '\n');
    console.log(JSON.stringify({ pages: results.length, failures, errors }));
    if (failures.length || errors.length) process.exitCode = 1;
  } finally { await browser.close(); }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
