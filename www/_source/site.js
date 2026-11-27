/* Shared, dependency-free browser behavior; also works with local file URLs. */
(() => {
  'use strict';
  const root = document.documentElement;
  const read = key => { try { return localStorage.getItem(key); } catch { return null; } };
  const write = (key, value) => { try { localStorage.setItem(key, value); } catch { /* Offline/private mode. */ } };
  root.dataset.theme = read('zima-site-theme') === 'light' ? 'light' : 'dark';
  document.querySelector('.theme-toggle')?.addEventListener('click', () => {
    root.dataset.theme = root.dataset.theme === 'light' ? 'dark' : 'light';
    write('zima-site-theme', root.dataset.theme);
  });
  const menu = document.querySelector('.menu-toggle');
  const nav = document.querySelector('#main-nav');
  menu?.addEventListener('click', () => {
    const expanded = nav.classList.toggle('open');
    menu.setAttribute('aria-expanded', String(expanded));
  });
  document.addEventListener('keydown', event => {
    if (event.key === 'Escape') {
      nav?.classList.remove('open');
      menu?.setAttribute('aria-expanded', 'false');
      document.querySelectorAll('details[open]').forEach(item => item.removeAttribute('open'));
    }
  });
  const lightbox = document.querySelector('.lightbox');
  let opener;
  document.querySelectorAll('.image-open').forEach(button => {
    button.addEventListener('click', () => {
      opener = button;
      const image = button.querySelector('img');
      const large = lightbox.querySelector('img');
      large.src = image.currentSrc || image.src;
      large.alt = image.alt;
      lightbox.querySelector('p').textContent = image.alt;
      lightbox.showModal();
    });
  });
  lightbox?.querySelector('.lightbox-close').addEventListener('click', () => lightbox.close());
  lightbox?.addEventListener('click', event => { if (event.target === lightbox) lightbox.close(); });
  lightbox?.addEventListener('close', () => opener?.focus());

  const showcase = document.querySelector('.showcase');
  if (showcase) {
    const slides = [...showcase.querySelectorAll('.slide')];
    const dots = [...showcase.querySelectorAll('.slide-dot')];
    if (slides.length > 1) {
      const motion = matchMedia('(prefers-reduced-motion: reduce)');
      let paused = motion.matches;
      let hover = false;
      let timer;
      const last = Number(read('zima-site-slide') ?? -1);
      const candidates = slides.map((_, i) => i).filter(i => i !== last);
      let current = candidates[Math.floor(Math.random() * candidates.length)];
      const pause = showcase.querySelector('.slide-pause');
      const schedule = () => {
        clearTimeout(timer);
        if (!paused && !hover && !document.hidden && !lightbox.open) timer = setTimeout(() => show(current + 1), Number(showcase.dataset.interval));
      };
      const show = index => {
        current = (index + slides.length) % slides.length;
        slides.forEach((slide, i) => {
          slide.hidden = i !== current;
          const video = slide.querySelector('video');
          if (video) {
            if (i === current && !paused) video.play().catch(() => {});
            else video.pause();
          }
        });
        dots.forEach((dot, i) => dot.setAttribute('aria-pressed', String(i === current)));
        write('zima-site-slide', String(current));
        schedule();
      };
      const updatePause = () => {
        pause.textContent = paused ? '▷' : 'Ⅱ';
        pause.setAttribute('aria-label', paused ? pause.dataset.play : pause.dataset.pause);
        const video = slides[current]?.querySelector('video');
        if (video) {
          if (paused) video.pause();
          else video.play().catch(() => {});
        }
      };
      showcase.querySelector('.slide-prev').addEventListener('click', () => show(current - 1));
      showcase.querySelector('.slide-next').addEventListener('click', () => show(current + 1));
      dots.forEach((dot, i) => dot.addEventListener('click', () => show(i)));
      pause.addEventListener('click', () => { paused = !paused; updatePause(); schedule(); });
      showcase.addEventListener('mouseenter', () => { hover = true; schedule(); });
      showcase.addEventListener('mouseleave', () => { hover = false; schedule(); });
      showcase.addEventListener('focusin', () => { hover = true; schedule(); });
      showcase.addEventListener('focusout', event => { if (!showcase.contains(event.relatedTarget)) { hover = false; schedule(); } });
      document.addEventListener('visibilitychange', () => {
        const video = slides[current]?.querySelector('video');
        if (video) {
          if (document.hidden || paused) video.pause();
          else video.play().catch(() => {});
        }
        schedule();
      });
      lightbox.addEventListener('close', schedule);
      motion.addEventListener('change', () => { paused = motion.matches; updatePause(); schedule(); });
      updatePause(); show(current);
    }
  }

  const search = document.querySelector('#guide-search');
  if (search) {
    const normalize = value => value.normalize('NFD').replace(/[\u0300-\u036f]/g, '').toLocaleLowerCase();
    search.addEventListener('input', () => {
      const query = normalize(search.value.trim());
      let count = 0;
      document.querySelectorAll('.guide-tile').forEach(tile => {
        tile.hidden = !normalize(tile.dataset.search).includes(query);
        if (!tile.hidden) count++;
      });
      document.querySelector('#no-results').hidden = count !== 0;
    });
  }
  document.querySelectorAll('[data-marker]').forEach(button => {
    button.addEventListener('click', () => {
      document.querySelectorAll('[data-marker]').forEach(other => other.setAttribute('aria-pressed', String(other.dataset.marker === button.dataset.marker)));
      if (button.classList.contains('hotspot')) document.querySelector('#marker-' + button.dataset.marker)?.scrollIntoView({ block: 'nearest', behavior: matchMedia('(prefers-reduced-motion: reduce)').matches ? 'instant' : 'smooth' });
    });
  });
  document.querySelectorAll('.annotated-image img').forEach(image => {
    const classify = () => { if (image.naturalHeight > image.naturalWidth) image.classList.add('portrait'); };
    if (image.complete) classify(); else image.addEventListener('load', classify);
  });
})();
