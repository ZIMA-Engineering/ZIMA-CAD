// One reading marker per page. Delegation also handles long, scrollable tables.
document.addEventListener('click', event => {
    const row = event.target.closest('tbody > tr');
    if (!row || event.target.closest('a, button, input, select, textarea')) return;
    document.querySelectorAll('tr.reference-selected-row').forEach(previous => {
        previous.classList.remove('reference-selected-row');
    });
    row.classList.add('reference-selected-row');
});
