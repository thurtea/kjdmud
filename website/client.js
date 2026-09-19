(function () {
  var logEl = document.getElementById("log");
  var urlBox = document.getElementById("url");
  var lineBox = document.getElementById("line");
  var statusEl = document.getElementById("status");
  var connectForm = document.getElementById("connect");
  var sendForm = document.getElementById("send");
  var disconnectBtn = document.getElementById("disconnect");
  var ws = null;

  var params = new URLSearchParams(window.location.search);
  if (params.get("url")) {
    urlBox.value = params.get("url");
  }

  function setStatus(text, state) {
    statusEl.textContent = text;
    if (state) statusEl.setAttribute("data-state", state);
    else statusEl.removeAttribute("data-state");
  }

  function add(text) {
    logEl.textContent += text;
    logEl.scrollTop = logEl.scrollHeight;
  }

  function disconnect() {
    if (ws) {
      ws.close();
      ws = null;
    }
  }

  function connect() {
    disconnect();
    var url = urlBox.value.trim();
    setStatus("Connecting to " + url + " ...");
    try {
      ws = new WebSocket(url);
    } catch (err) {
      setStatus("Invalid URL", "error");
      add("[error] " + err + "\n");
      return;
    }
    ws.onopen = function () {
      setStatus("Connected to " + url, "open");
      add("[connected to " + url + "]\n");
    };
    ws.onclose = function () {
      setStatus("Disconnected");
      add("[disconnected]\n");
      ws = null;
    };
    ws.onerror = function () {
      setStatus("Socket error", "error");
      add("[socket error]\n");
    };
    ws.onmessage = function (ev) {
      add(typeof ev.data === "string" ? ev.data : "[binary]\n");
    };
  }

  connectForm.addEventListener("submit", function (ev) {
    ev.preventDefault();
    connect();
  });

  disconnectBtn.addEventListener("click", function () {
    disconnect();
  });

  sendForm.addEventListener("submit", function (ev) {
    ev.preventDefault();
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(lineBox.value + "\n");
    } else {
      add("[not connected]\n");
    }
    lineBox.value = "";
    lineBox.focus();
  });

  connect();
})();
