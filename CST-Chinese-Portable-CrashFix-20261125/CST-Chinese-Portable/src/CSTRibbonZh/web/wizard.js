function (dictionary, expectedUrl) {
    'use strict';
    const canonical = value => { const u = new URL(value); u.hash = ''; u.search = ''; return decodeURIComponent(u.href).toLowerCase(); };
    if (location.protocol !== 'file:' || canonical(location.href) !== canonical(expectedUrl)) return;
    if (window.__cstZhWizardV1 || !document.body) return;
    const normalize = s => s.replace(/\s+/g, ' ').trim().toLowerCase();
    const terms = new Map(Object.entries(dictionary).map(([a, b]) => [normalize(a), b]));
    const selector = [
        '#myHeader', '#applicationsPage h2', '#map li a',
        '#applicationOptionsListView h3', '#workflowPage h2', '#workflowOptionsListView h3',
        '#solverPage h2', '#solver .solverTitle', '#solverListView .solverTitle',
        '#solverListView .solverDescription', '#unitsPage h2', '#unitsPage label',
        '#settingsPage h2', '#settingsPage label', '#defineAtInfo',
        '#summaryPage h2', '#summaryPage > label', '#summaryTextArea th',
        '#WorkflowPath', '#wizardButtons button'
    ].join(',');
    const excluded = 'script,style,input,textarea,select,option,[contenteditable],#settingsMinUnit,#settingsMaxUnit,#multiValueUnit';
    function translate(text, breadcrumb) {
        if (breadcrumb && text.includes('|')) return text.split('|').map(part => translate(part, false)).join('|');
        const key = normalize(text);
        let value = terms.get(key);
        if (!value && key.endsWith(':')) {
            const base = terms.get(key.slice(0, -1));
            if (base) value = base + '：';
        }
        if (!value) return text;
        return (text.match(/^\s*/)[0]) + value + (text.match(/\s*$/)[0]);
    }
    function sweep() {
        document.querySelectorAll(selector).forEach(root => {
            const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
            const nodes = [];
            while (walker.nextNode()) nodes.push(walker.currentNode);
            nodes.forEach(node => {
                if (!node.parentElement || node.parentElement.closest(excluded)) return;
                const value = translate(node.nodeValue, !!node.parentElement.closest('#WorkflowPath'));
                if (value !== node.nodeValue) node.nodeValue = value;
            });
        });
    }
    let pending = false;
    const observer = new MutationObserver(() => {
        if (pending) return;
        pending = true;
        setTimeout(() => {
            pending = false;
            observer.disconnect();
            sweep();
            observe();
        }, 40);
    });
    function observe() { observer.observe(document.body, {childList: true, subtree: true, characterData: true}); }
    sweep();
    observe();
    window.__cstZhWizardV1 = true;
}
