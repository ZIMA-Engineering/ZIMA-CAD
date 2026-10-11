(function () {
  if (window.__zimaPasswordManagerInitialized)
    return;
  window.__zimaPasswordManagerInitialized = true;

  if (!/^https?:$/.test(window.location.protocol))
    return;

  function isUsableInput(input) {
    return !!input && !input.disabled && !input.readOnly;
  }

  function looksLikeUsernameInput(input) {
    if (!isUsableInput(input))
      return false;

    var type = (input.type || 'text').toLowerCase();
    return type === 'text' || type === 'email' || type === 'tel';
  }

  function resolvedActionUrl(form) {
    try {
      return new URL(form.getAttribute('action') || window.location.href, window.location.href).href;
    } catch (error) {
      return window.location.href;
    }
  }

  function actionOrigin(url) {
    try {
      return new URL(url).origin;
    } catch (error) {
      return '';
    }
  }

  function actionPath(url) {
    try {
      return new URL(url).pathname || '/';
    } catch (error) {
      return '/';
    }
  }

  function createRequestId() {
    return String(Date.now()) + ':' + Math.random();
  }

  function setInputValue(input, value) {
    if (!input)
      return;

    var prototype = Object.getPrototypeOf(input);
    var descriptor = Object.getOwnPropertyDescriptor(prototype, 'value');
    if (descriptor && descriptor.set)
      descriptor.set.call(input, value);
    else
      input.value = value;

    input.dispatchEvent(new Event('input', { bubbles: true }));
    input.dispatchEvent(new Event('change', { bubbles: true }));
  }

  function withInternalUpdate(state, callback) {
    state.internalUpdate = true;
    try {
      callback();
    } finally {
      state.internalUpdate = false;
    }
  }

  function pickPasswordInput(form) {
    var passwordInputs = Array.from(form.querySelectorAll('input[type="password"]')).filter(isUsableInput);
    if (passwordInputs.length !== 1)
      return null;

    var passwordInput = passwordInputs[0];
    var autocomplete = (passwordInput.getAttribute('autocomplete') || '').toLowerCase();
    if (autocomplete.indexOf('new-password') !== -1)
      return null;

    return passwordInput;
  }

  function pickUsernameInput(form, passwordInput) {
    var inputs = Array.from(form.querySelectorAll('input'));
    var passwordIndex = inputs.indexOf(passwordInput);

    for (var i = passwordIndex - 1; i >= 0; --i) {
      if (looksLikeUsernameInput(inputs[i]))
        return inputs[i];
    }

    for (var j = 0; j < inputs.length; ++j) {
      if (inputs[j] !== passwordInput && looksLikeUsernameInput(inputs[j]))
        return inputs[j];
    }

    return null;
  }

  function buildAutofillRequest(state) {
    var actionUrl = resolvedActionUrl(state.form);
    return {
      requestId: state.requestId,
      pageUrl: window.location.href,
      origin: window.location.origin,
      actionUrl: actionUrl,
      actionOrigin: actionOrigin(actionUrl),
      actionPath: actionPath(actionUrl),
      usernameFieldName: state.usernameInput.name || '',
      usernameFieldId: state.usernameInput.id || '',
      passwordFieldName: state.passwordInput.name || '',
      passwordFieldId: state.passwordInput.id || '',
      usernameValue: state.usernameInput.value || ''
    };
  }

  function hasSavedUsername(state, username) {
    return !!username && state.availableUsernames.indexOf(username) !== -1;
  }

  function removePendingRequest(state) {
    if (!state.requestId)
      return;

    delete state.pendingRequests[state.requestId];
    state.requestId = '';
  }

  function requestAutofill(state) {
    removePendingRequest(state);
    state.requestId = createRequestId();
    state.pendingRequests[state.requestId] = state;
    state.bridge.requestAutofill(buildAutofillRequest(state));
  }

  function currentChooserValue(state) {
    var currentUsername = state.usernameInput.value || '';
    if (hasSavedUsername(state, currentUsername))
      return currentUsername;

    if (hasSavedUsername(state, state.activeUsername))
      return state.activeUsername;

    return '';
  }

  function syncChooserSelection(state) {
    if (!state.chooserSelect)
      return;

    state.chooserSelect.value = currentChooserValue(state);
  }

  function clearAutofilledPassword(state) {
    withInternalUpdate(state, function () {
      setInputValue(state.passwordInput, '');
    });
    state.activeUsername = '';
    state.passwordAutofilled = false;
    state.passwordEditedByUser = false;
    syncChooserSelection(state);
  }

  function removeAccountChooser(state) {
    if (state.chooserHost && state.chooserHost.parentNode)
      state.chooserHost.parentNode.removeChild(state.chooserHost);

    state.chooserHost = null;
    state.chooserTitle = null;
    state.chooserSelect = null;
  }

  function ensureAccountChooser(state) {
    if (state.availableUsernames.length <= 1) {
      removeAccountChooser(state);
      return;
    }

    if (state.chooserSelect)
      return;

    var host = document.createElement('div');
    host.style.display = 'block';
    host.style.marginTop = '0.5rem';
    host.style.maxWidth = '18rem';

    var shadow = host.attachShadow({ mode: 'closed' });
    var style = document.createElement('style');
    style.textContent = [
      ':host { all: initial; }',
      '.zima-account-chooser {',
      '  display: grid;',
      '  gap: 0.25rem;',
      '  font: 13px sans-serif;',
      '  color: #2f2f2f;',
      '}',
      '.zima-account-chooser-title {',
      '  font-weight: 600;',
      '}',
      '.zima-account-chooser-select {',
      '  font: inherit;',
      '  padding: 0.25rem 0.4rem;',
      '  max-width: 100%;',
      '}'
    ].join('\n');

    var label = document.createElement('label');
    label.className = 'zima-account-chooser';

    var title = document.createElement('span');
    title.className = 'zima-account-chooser-title';

    var select = document.createElement('select');
    select.className = 'zima-account-chooser-select';
    select.addEventListener('change', function () {
      if (state.internalUpdate)
        return;

      var selectedUsername = select.value || '';
      if (!selectedUsername)
        return;

      withInternalUpdate(state, function () {
        setInputValue(state.usernameInput, selectedUsername);
      });
      requestAutofill(state);
    });

    shadow.appendChild(style);
    shadow.appendChild(label);
    label.appendChild(title);
    label.appendChild(select);
    state.form.appendChild(host);

    state.chooserHost = host;
    state.chooserTitle = title;
    state.chooserSelect = select;
  }

  function refreshAccountChooser(state) {
    if (state.availableUsernames.length <= 1) {
      removeAccountChooser(state);
      return;
    }

    ensureAccountChooser(state);

    if (!state.chooserSelect)
      return;

    state.chooserTitle.textContent = state.savedAccountsLabel || '';
    state.chooserSelect.setAttribute('aria-label', state.chooseAccountLabel || '');
    state.chooserSelect.title = state.chooseAccountLabel || '';

    while (state.chooserSelect.firstChild) {
      state.chooserSelect.removeChild(state.chooserSelect.firstChild);
    }

    var placeholder = document.createElement('option');
    placeholder.value = '';
    placeholder.textContent = state.chooseAccountPlaceholder || '';
    state.chooserSelect.appendChild(placeholder);

    state.availableUsernames.forEach(function (username) {
      var option = document.createElement('option');
      option.value = username;
      option.textContent = username;
      state.chooserSelect.appendChild(option);
    });

    syncChooserSelection(state);
  }

  function handleUsernameEdited(state) {
    if (state.internalUpdate)
      return;

    var currentUsername = state.usernameInput.value || '';
    if (state.activeUsername && currentUsername !== state.activeUsername) {
      if (state.passwordAutofilled && !state.passwordEditedByUser)
        clearAutofilledPassword(state);
      else
        state.activeUsername = '';
    }

    syncChooserSelection(state);

    if (hasSavedUsername(state, currentUsername) && currentUsername !== state.activeUsername)
      requestAutofill(state);
  }

  function handlePasswordEdited(state) {
    if (state.internalUpdate)
      return;

    state.passwordAutofilled = false;
    state.passwordEditedByUser = true;
  }

  function bindForm(form, bridge, pendingRequests) {
    if (form.__zimaState)
      return;

    var passwordInput = pickPasswordInput(form);
    if (!passwordInput)
      return;

    var usernameInput = pickUsernameInput(form, passwordInput);
    if (!usernameInput)
      return;

    var state = {
      requestId: '',
      bridge: bridge,
      pendingRequests: pendingRequests,
      form: form,
      usernameInput: usernameInput,
      passwordInput: passwordInput,
      availableUsernames: [],
      activeUsername: '',
      passwordAutofilled: false,
      passwordEditedByUser: false,
      internalUpdate: false,
      savedAccountsLabel: '',
      chooseAccountLabel: '',
      chooseAccountPlaceholder: '',
      chooserHost: null,
      chooserTitle: null,
      chooserSelect: null
    };

    form.__zimaState = state;

    form.addEventListener('submit', function () {
      var actionUrl = resolvedActionUrl(form);
      bridge.formSubmitted({
        pageUrl: window.location.href,
        origin: window.location.origin,
        actionUrl: actionUrl,
        actionOrigin: actionOrigin(actionUrl),
        actionPath: actionPath(actionUrl),
        usernameFieldName: usernameInput.name || '',
        usernameFieldId: usernameInput.id || '',
        passwordFieldName: passwordInput.name || '',
        passwordFieldId: passwordInput.id || '',
        username: usernameInput.value || '',
        password: passwordInput.value || ''
      });
    }, true);

    usernameInput.addEventListener('input', function () {
      handleUsernameEdited(state);
    });
    usernameInput.addEventListener('change', function () {
      handleUsernameEdited(state);
    });
    passwordInput.addEventListener('input', function () {
      handlePasswordEdited(state);
    });
    passwordInput.addEventListener('change', function () {
      handlePasswordEdited(state);
    });

    requestAutofill(state);
  }

  function handleAutofillReady(payload, pendingRequests) {
    if (!payload || !payload.requestId)
      return;

    var state = pendingRequests[payload.requestId];
    delete pendingRequests[payload.requestId];
    if (!state || state.requestId !== payload.requestId)
      return;

    state.requestId = '';

    state.availableUsernames = Array.isArray(payload.availableUsernames)
      ? payload.availableUsernames.filter(function (username) {
        return typeof username === 'string' && username.length > 0;
      })
      : [];
    state.savedAccountsLabel = payload.savedAccountsLabel || '';
    state.chooseAccountLabel = payload.chooseAccountLabel || '';
    state.chooseAccountPlaceholder = payload.chooseAccountPlaceholder || '';

    refreshAccountChooser(state);

    var hasFilledCredential = typeof payload.username === 'string'
      && payload.username !== ''
      && typeof payload.password === 'string'
      && payload.password !== '';
    if (hasFilledCredential) {
      withInternalUpdate(state, function () {
        setInputValue(state.usernameInput, payload.username);
        setInputValue(state.passwordInput, payload.password);
      });
      state.activeUsername = payload.username;
      state.passwordAutofilled = true;
      state.passwordEditedByUser = false;
      syncChooserSelection(state);
      return;
    }

    var currentUsername = state.usernameInput.value || '';
    if (!hasSavedUsername(state, currentUsername)) {
      if (state.passwordAutofilled && !state.passwordEditedByUser)
        clearAutofilledPassword(state);
      else
        state.activeUsername = '';
    }

    syncChooserSelection(state);
  }

  function scanDocument(bridge, pendingRequests) {
    Array.from(document.forms || []).forEach(function (form) {
      bindForm(form, bridge, pendingRequests);
    });
  }

  function initialize(channel) {
    var bridge = channel.objects.zimaPasswordManager;
    if (!bridge)
      return;

    var pendingRequests = {};

    bridge.autofillReady.connect(function (payload) {
      handleAutofillReady(payload, pendingRequests);
    });

    var scheduleScan = (function () {
      var timer = 0;
      return function () {
        clearTimeout(timer);
        timer = setTimeout(function () {
          scanDocument(bridge, pendingRequests);
        }, 100);
      };
    })();

    scanDocument(bridge, pendingRequests);

    new MutationObserver(scheduleScan).observe(document.documentElement, {
      childList: true,
      subtree: true
    });
  }

  if (typeof qt === 'undefined' || !qt.webChannelTransport)
    return;

  new QWebChannel(qt.webChannelTransport, initialize);
})();
