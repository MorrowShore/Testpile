// SPDX-License-Identifier: GPL-3.0-or-later
import { UAParser } from "ua-parser-js";

(function () {
  "use strict";
  const cachebuster = document.documentElement.dataset.cachebuster;
  const cachebusterSuffix = cachebuster
    ? `?cachebuster=${encodeURIComponent(cachebuster)}`
    : "";

  function isString(arg) {
    return typeof arg === "string";
  }

  function tagAppendChild(t, child) {
    t.appendChild(isString(child) ? document.createTextNode(child) : child);
  }

  function tagAppendChildren(t, children) {
    if (children) {
      if (Array.isArray(children)) {
        for (const child of children) {
          tagAppendChild(t, child);
        }
      } else {
        tagAppendChild(t, children);
      }
    }
  }

  function tag(name, attributesOrChildren, optionalChildren) {
    const t = document.createElement(name);
    if (Array.isArray(attributesOrChildren) || isString(attributesOrChildren)) {
      tagAppendChildren(t, attributesOrChildren);
    } else if (attributesOrChildren) {
      for (const [key, value] of Object.entries(attributesOrChildren)) {
        if (key === "className") {
          t.className = value;
        } else {
          t.setAttribute(key, value);
        }
      }
    }
    tagAppendChildren(t, optionalChildren);
    return t;
  }

  function stringify(arg) {
    return "" + arg;
  }

  function getQueryParams() {
    return new URLSearchParams(window.location.search);
  }

  function isTrueParam(param) {
    if (param) {
      const s = stringify(param).toLowerCase();
      return s !== "" && s !== "0" && !s.startsWith("f") && !s.startsWith("n");
    }
    return false;
  }

  function looksLikeLocalhost(host) {
    return /^(localhost|127\.0\.0\.1|::1)(:|$)/i.test(host);
  }

  function makeScheme(scheme, host) {
    if (!scheme) {
      scheme = looksLikeLocalhost(host) ? "ws://" : "wss://";
    }
    if (!scheme.endsWith("://")) {
      scheme += "://";
    }
    return scheme;
  }

  function makeAbsolutePath(path) {
    if (path.startsWith("/")) {
      return path;
    } else {
      let prefix = window.location.pathname.replace(/[^\/]+$/, "");
      if (!prefix.endsWith("/")) {
        prefix += "/";
      }
      if (!prefix.startsWith("/")) {
        prefix = "/" + prefix;
      }
      return prefix + path;
    }
  }

  function getUrlHost(params) {
    return (params.get("host") || window.location.host || "").trim();
  }

  function getUrlArgument(params) {
    const host = getUrlHost(params);
    const scheme = makeScheme(params.get("scheme"), host);
    const path = makeAbsolutePath(params.get("path") || "/drawpile-web/ws");
    let url = encodeURI(scheme + host + path);

    const session = params.get("session");
    if (session) {
      url += "?session=" + encodeURIComponent(session);
      const password = window.location.hash?.replace(/^#/, "");
      if (password) {
        url += "&p=" + encodeURIComponent(password);
      }
    }

    return url;
  }

  function parseAssets(buffer, size) {
    const assets = [];
    const view = new DataView(buffer);
    const decoder = new TextDecoder();
    let offset = 0;
    while (offset < size) {
      const nameLength = view.getUint32(offset, true);
      offset += 4;
      const name = decoder.decode(new Uint8Array(buffer, offset, nameLength));
      offset += nameLength;
      const contentLength = view.getUint32(offset, true);
      offset += 4;
      const content = new Uint8Array(buffer, offset, contentLength);
      offset += contentLength;
      assets.push([name, content]);
    }
    return assets;
  }

  function allocateMemory(log, warn) {
    // Instead of clamping like the documentation says, Safari just dies if too
    // large a maximum memory is requested. So we have to keep trying this.
    // The unit here is WebAssembly pages, which are 64KiB large.
    const PAGE = 65536; // In bytes, 64 KiB
    const INITIAL = 2048; // In pages, 128 MiB
    const BACKOFF = 4096; // In pages, 256 MiB
    let maximum = 65536; // In pages, 4 GiB
    let memory;
    while (!(memory && memory.buffer) && maximum >= BACKOFF) {
      try {
        log(`Attempting to allocate ${maximum} pages`);
        memory = new WebAssembly.Memory({
          initial: INITIAL,
          maximum: maximum,
          shared: true,
        });
      } catch (e) {
        warn(`Failed to allocate ${maximum} pages: ${e}`);
        maximum -= BACKOFF;
      }
    }

    if (!memory) {
      throw Error(
        "Memory allocation failed. This can happen in some browsers if you " +
          "refresh. Close this page and your browser entirely, then try again.",
      );
    }

    const shared = memory.buffer instanceof SharedArrayBuffer;
    log(
      `Allocated ${memory.buffer?.constructor?.name || "memory"}, ` +
        `initial ${memory.buffer.byteLength}/${INITIAL * PAGE} bytes, ` +
        `maximum ${maximum * PAGE} bytes, shared ${shared}`,
    );

    // The Emscripten generated code has a check like this.
    if (!shared) {
      throw Error(
        "Memory allocated is not a SharedArrayBuffer, even though your browser " +
          "supports it. You may need to change a setting.",
      );
    }

    return [memory, maximum * PAGE];
  }

  function removeProgressBar() {
    document.querySelector("#progress-bar")?.remove();
  }

  function getOrAddProgressBar() {
    let progressBar = document.querySelector("#progress-bar");
    if (!progressBar) {
      progressBar = tag("div", { id: "progress-bar" }, [
        tag("progress", { "aria-labelledby": "progress-description" }),
        tag("div", { id: "progress-description" }),
        tag("div", { id: "progress-size" }),
      ]);
      document.querySelector("#init").appendChild(progressBar);
    }
    return progressBar;
  }

  function toMibString(size) {
    return (size / (1024 * 1024)).toFixed(0);
  }

  function showProgressBar(title, done, total) {
    const progressBar = getOrAddProgressBar();
    const percent =
      total === 0 ? 100.0 : Math.floor((done / total) * 100.0 + 0.5);
    const percentText = `${percent.toFixed(0)}%`;

    const description = progressBar.querySelector("#progress-description");
    description.textContent = `${title} ${percentText}`;

    const size = progressBar.querySelector("#progress-size");
    size.textContent = `${toMibString(done)} / ${toMibString(total)} MiB`;

    const progress = progressBar.querySelector("progress");
    progress.value = `${done}`;
    progress.max = `${total}`;
    progress.textContent = percentText;
  }

  function showBusyStatus(text) {
    const status = document.querySelector("#status");
    status.textContent = text;
    status.setAttribute("aria-busy", "true");
  }

  function showErrorStatus(text) {
    const status = document.querySelector("#status");
    status.textContent = text;
    status.removeAttribute("aria-busy");
    removeProgressBar();
  }

  function showScreen() {
    document.querySelector("#init").remove();
    document.querySelector("#screen").style.display = null;
    document.documentElement.classList.add("fullscreen");
    document.body.classList.add("fullscreen");
  }

  function registerMessageHandler(
    drawpileHandleBrowserAuth,
    stringToNewUTF8,
    UTF8ToString,
  ) {
    window.addEventListener("message", (e) => {
      const loginModal = document.querySelector("#login-modal");
      if (loginModal) {
        const type = e.data?.type;
        if (type === "auth-username-selected") {
          drawpileHandleBrowserAuth(1, stringToNewUTF8(`${e.data.username}`));
        } else if (type === "auth-identified") {
          drawpileHandleBrowserAuth(2, stringToNewUTF8(e.data.token));
          loginModal.remove();
        }
      }
    });

    window.drawpileShowLoginModal = function () {
      if (!document.querySelector("#login-modal")) {
        const modal = tag("div", { id: "login-modal", className: "modal" }, [
          tag("div", { className: "modal-container" }, [
            tag("div", { className: "modal-body" }, [
              tag("div", { className: "modal-title" }, [
                tag("img", {
                  src: `drawpile.svg${cachebusterSuffix}`,
                  alt: "Drawpile logo",
                }),
                tag("h2", { className: "modal-heading" }, "Login"),
                tag(
                  "button",
                  { className: "modal-close-button", title: "Close" },
                  "\u00d7",
                ),
              ]),
              tag("iframe", {
                title: "drawpile.net login",
                src: "https://drawpile.net/auth/",
              }),
            ]),
          ]),
        ]);
        modal
          .querySelector(".modal-close-button")
          .addEventListener("click", () => {
            modal.remove();
            drawpileHandleBrowserAuth(-1, stringToNewUTF8(""));
          });
        document.body.prepend(modal);
      }
    };

    window.drawpileCancelLoginModal = function () {
      const modal = document.querySelector("#login-modal");
      if (modal) {
        modal.remove();
      }
    };

    window.drawpileAuthenticate = function (data, len) {
      const iframe = document.querySelector("#login-modal iframe");
      if (iframe) {
        const payload = UTF8ToString(data, len);
        iframe.contentWindow.postMessage(
          {
            type: "auth-authenticate",
            args: JSON.parse(payload),
          },
          "*",
        );
      }
    };
  }

  function registerUnloadHandler(shouldPreventUnload) {
    window.addEventListener("beforeunload", (e) => {
      if (shouldPreventUnload()) {
        e.preventDefault();
      }
    });
  }

  // SPDX-SnippetBegin
  // SPDX-License-Identifier: GPL-2.0-or-later
  // SDPX—SnippetName: WASM loading progress from wordpress-playground
  function patchInstantiateStreaming(assetSize, wasmSize) {
    function onProgress(done) {
      try {
        showProgressBar(
          "Loading application",
          assetSize + Math.min(done, wasmSize),
          assetSize + wasmSize,
        );
      } catch (e) {
        console.error("Error showing WASM streaming progress", e);
      }
    }

    const originalInstantiateStreaming = WebAssembly.instantiateStreaming;
    WebAssembly.instantiateStreaming = (response, ...args) => {
      const reportingResponse = new Response(
        new ReadableStream(
          {
            async start(controller) {
              const reader = response.clone().body.getReader();
              let loaded = 0;
              for (;;) {
                const { done, value } = await reader.read();
                if (done) {
                  onProgress(wasmSize);
                  break;
                }
                loaded += value.byteLength;
                onProgress(loaded);
                controller.enqueue(value);
              }
              controller.close();
            },
          },
          {
            status: response.status,
            statusText: response.statusText,
          },
        ),
      );
      for (const pair of response.headers.entries()) {
        reportingResponse.headers.set(pair[0], pair[1]);
      }

      return originalInstantiateStreaming(reportingResponse, ...args);
    };
  }
  // SPDX-SnippetEnd

  // Apple pencils have ludicrously low pressure, meaning that with a normal,
  // linear pressure curve, lines will come out almost invisibly thin unless the
  // user presses down with enough force to pierce their pen through the screen.
  // We can't actually determine if the user has such a pen, so we guess.
  window.drawpileHasLowPressurePen = function () {
    try {
      const ua = new UAParser();
      const device = ua.getDevice?.model || "";
      const ios =
        device.indexOf("iPad") !== -1 || device.indexOf("iPhone") !== -1;
      return ios ? 1 : 0;
    } catch (e) {
      console.error(e);
      return 0;
    }
  };

  function getSizeFromDocument(key) {
    const value = document.documentElement.dataset[key];
    if (value) {
      const size = parseInt(value, 10);
      if (!Number.isNaN(size) && size > 0) {
        return size;
      }
    }
    return 0;
  }

  async function load(assetSize, wasmSize) {
    screen = document.createElement("div");
    screen.id = "screen";
    screen.style.display = "none";
    document.body.appendChild(screen);

    const params = getQueryParams();
    const config = {
      qt: {
        onExit: (exitData) => {
          let message = "Application exit";
          if (exitData.code !== undefined) {
            message += ` with code ${exitData.code}`;
          }
          if (exitData.message !== undefined) {
            message += `(${exitData.text})`;
          }
          alert(message);
        },
        entryFunction: window.createQtAppInstance,
        containerElements: [screen],
      },
      arguments: ["--single-session", getUrlArgument(params)],
    };

    config.qt.onLoaded = () => {
      showScreen();
      registerUnloadHandler(config._drawpileShouldPreventUnload);
      registerMessageHandler(
        config._drawpileHandleBrowserAuth,
        config.stringToNewUTF8,
        config.UTF8ToString,
      );
    };

    let assetBundlePath = "assets.bundle";
    if (cachebuster) {
      assetBundlePath += cachebusterSuffix;
      config.locateFile = (path, scriptDirectory) => {
        return (scriptDirectory || "") + path + cachebusterSuffix;
      };
    }

    if (isTrueParam(params.get("console"))) {
      const con = document.createElement("div");
      con.id = "console";
      let consoleSize = Number.parseInt(params.get("consolesize"), 10);
      if (Number.isNaN(consoleSize) || consoleSize < 1 || consoleSize > 99) {
        consoleSize = 20;
      }
      con.style.height = `${consoleSize}%`;
      document.body.prepend(con);
      screen.style.height = `${100 - consoleSize}%`;
      const appendToConsole = (className, args) => {
        const line = tag("span", { className }, [
          args.map(stringify).join(" "),
        ]);
        con.appendChild(line);
        line.scrollIntoView();
      };
      config.print = function (...args) {
        console.log(...args);
        appendToConsole("console-log", args);
      };
      config.printErr = function (...args) {
        console.warn(...args);
        appendToConsole("console-warn", args);
      };
      window.addEventListener("error", (e) => {
        appendToConsole("console-error", ["Uncaught:", stringify(e.message)]);
        return true;
      });
      window.addEventListener("unhandledrejection", (e) => {
        appendToConsole("console-error", [
          "Uncaught in promise:",
          stringify(e.reason),
        ]);
        return true;
      });
      appendToConsole("console-log", [
        "Arguments:",
        JSON.stringify(config.arguments),
      ]);
    }

    const response = await fetch(assetBundlePath);
    const reader = response.body.getReader();
    let accumulator = new Uint8Array(assetSize);
    let done = 0;
    while (true) {
      showProgressBar(
        "Loading assets",
        Math.min(done, assetSize),
        assetSize + wasmSize,
      );
      const readResult = await reader.read();
      if (readResult.done) {
        break;
      } else {
        const value = readResult.value;
        const requiredLength = done + value.byteLength;
        if (requiredLength > accumulator.byteLength) {
          let nextAccumulator = new Uint8Array(requiredLength);
          nextAccumulator.set(accumulator, 0);
          accumulator = nextAccumulator;
        }
        accumulator.set(value, done);
        done += value.byteLength;
      }
    }
    showProgressBar("Loading assets", assetSize, assetSize + wasmSize);

    const assets = parseAssets(accumulator.buffer, done);
    config.preRun = [
      (instance) => {
        while (assets.length !== 0) {
          const [name, content] = assets.shift();
          const dir = instance.PATH.dirname(name);
          if (dir && dir !== "" && dir !== "/") {
            instance.FS.mkdirTree(dir);
          }
          instance.FS.writeFile(name, content);
        }
      },
    ];

    return config;
  }

  async function initialize(config) {
    const [memory, heapMax] = allocateMemory(
      config.print || console.log,
      config.printErr || console.warn,
    );
    config.wasmMemory = memory;
    // Clamp to 4GiB - one page (64 KiB). This is what Emscripten's library.js
    // does to avoid some edge cases that happen at the full 4 GB.
    config.DRAWPILE_HEAP_MAX = Math.min(4294901760, heapMax);
    await window.qtLoad(config);
  }

  async function start() {
    try {
      const assetSize = getSizeFromDocument("assetsize");
      const wasmSize = getSizeFromDocument("wasmsize");
      patchInstantiateStreaming(assetSize, wasmSize);
      showBusyStatus("Loading, this may take a while…");
      showProgressBar("Preparing", 0, assetSize + wasmSize);
      const config = await load(assetSize, wasmSize);
      showBusyStatus("Initializing, this may take a while…");
      await initialize(config);
    } catch (e) {
      console.error(e);
      showErrorStatus(`Failed to start Drawpile: ${e}`);
      throw e;
    }
  }

  function checkSharedArrayBuffer() {
    if (typeof SharedArrayBuffer === "function") {
      return true;
    } else {
      const missing = [];
      if (!window.crossOriginIsolated) {
        missing.push("cross-origin isolation");
      }
      if (!window.isSecureContext) {
        missing.push("a secure context");
      }
      const reason =
        missing.length === 0
          ? tag("p", [
              "Looks like your browser doesn't support it. For more " +
                "information, check out ",
              tag(
                "a",
                {
                  href: "https://drawpile.net/sharedarraybufferhelp/",
                  target: "_blank",
                },
                ["this help page"],
              ),
              ".",
            ])
          : tag("p", [
              "Looks like this is due to a server misconfiguration, " +
                "tell the owner of " +
                window.location.hostname +
                " that they are missing " +
                missing.join(" and ") +
                ". For more information, check out ",
              tag(
                "a",
                {
                  href: "https://drawpile.net/sharedarraybufferhelp/",
                  target: "_blank",
                },
                ["this help page"],
              ),
              ".",
            ]);

      const status = document.querySelector("#status");
      status.appendChild(
        tag("p", [
          tag("strong", ["Fatal error:"]),
          " SharedArrayBuffer not available.",
        ]),
      );
      status.appendChild(reason);
      return false;
    }
  }

  function checkHost() {
    const host = getUrlHost(getQueryParams()).toLowerCase();
    const invalid =
      host === "" ||
      host === "drawpile.net" ||
      host === "web.drawpile.net" ||
      host === "www.drawpile.net";
    if (invalid) {
      const status = document.querySelector("#status");
      status.appendChild(tag("p", [tag("strong", ["Invalid session link."])]));
      status.appendChild(
        tag("p", [
          "However you got here was not via a valid link to a Drawpile session.",
        ]),
      );
      return false;
    } else {
      return true;
    }
  }

  async function showStartup() {
    const startup = document.querySelector("#startup");
    startup.appendChild(
      tag("p", [
        "This browser version of Drawpile is still ",
        tag("strong", ["in development"]),
        ". Some things may not work properly yet. The following are ",
        tag("strong", ["known issues"]),
        ":",
      ]),
    );
    startup.appendChild(
      tag("ul", [
        tag("li", [
          "Some settings, such as brush presets or avatars, won't be saved.",
        ]),
        tag("li", [
          "Keys may get stuck in a held-down state until pressed again.",
        ]),
        tag("li", ["Firefox: Browser steals Ctrl+Z for itself."]),
      ]),
    );
    startup.appendChild(
      tag("p", [
        "If you find an issue not listed here, please report it! Check out ",
        tag("a", { href: "https://drawpile.net/help/", target: "_blank" }, [
          "the help page on drawpile.net",
        ]),
        " for ways to do so.",
      ]),
    );
    startup.appendChild(
      tag("p", [
        "The application for Windows, macOS, Linux and Android is available ",
        tag("a", { href: "https://drawpile.net/download/", target: "_blank" }, [
          "on drawpile.net",
        ]),
        ".",
      ]),
    );

    const updateLoading = tag("p", { "aria-busy": "true" }, [
      "Checking for updates…",
    ]);
    const updateDiv = tag("div", { className: "update" }, [updateLoading]);
    startup.appendChild(updateDiv);

    let upToDate = false;
    const commit = document.documentElement.dataset.commit || "-missing-";
    try {
      if (isTrueParam(getQueryParams().get("blockupdatecheck"))) {
        throw Error("Update check blocked");
      }

      const updateUrl = `https://web.drawpile.net/update.json?cachebuster=${Date.now()}`;
      const res = await fetch(updateUrl, { signal: AbortSignal.timeout(5000) });
      const u = await res.json();

      try {
        const haveMessage = u.message && isString(u.message);
        const haveLink = u.link && isString(u.link);
        if (haveLink || haveMessage) {
          const notice = tag("div", { className: "update-notice" });
          if (haveMessage) {
            notice.appendChild(tag("p", [u.message]));
          }
          if (haveMessage) {
            notice.appendChild(
              tag("p", [tag("a", { href: u.link, target: "_blank" }, u.link)]),
            );
          }
          updateDiv.appendChild(notice);
        }
      } catch (e) {
        console.error(e);
      }

      upToDate = commit === u.commit;
      if (upToDate) {
        updateDiv.appendChild(
          tag("p", [
            "✔️ This installation is up to date at version ",
            tag("code", [commit]),
            ".",
          ]),
        );
      } else {
        updateDiv.appendChild(
          tag("p", [
            tag("strong", ["⛔️ Warning:"]),
            " This installation is ",
            tag("strong", ["outdated"]),
            " at version ",
            tag("code", [commit]),
            ", which is not the most recent version ",
            tag("code", [u.commit]),
            ".",
          ]),
        );
        updateDiv.appendChild(
          tag("p", [
            "Try refreshing the page. If that doesn't change anything, " +
              "notify the server owner to update.",
          ]),
        );
        updateDiv.appendChild(
          tag("p", [
            "You can continue regardless, but things may not work properly.",
          ]),
        );
      }
    } catch (e) {
      console.error(e);
      updateDiv.appendChild(
        tag("p", [
          tag("strong", ["⚠️ Warning:"]),
          " Could not determine if installation is up to date or not. " +
            "It may be outdated at version ",
          tag("code", [commit]),
          ".",
        ]),
      );
      updateDiv.appendChild(
        tag("p", [
          "Try refreshing the page. If that doesn't change anything, " +
            "consult the server owner or check out ",
          tag(
            "a",
            { href: "https://drawpile.net/help/" },
            "the help page on drawpile.net",
          ),
          " on how to get in contact with someone who can check what's going on.",
        ]),
      );
      updateDiv.appendChild(
        tag("p", [
          "You can continue regardless, but things may not work properly.",
        ]),
      );
    } finally {
      updateLoading.remove();
      const button = tag("button", { class: upToDate ? "primary" : "danger" }, [
        upToDate ? "Start" : "Start Anyway",
      ]);
      button.addEventListener("click", (_event) => {
        startup.remove();
        start();
      });
      startup.appendChild(button);
    }
  }

  if (checkSharedArrayBuffer() && checkHost()) {
    showStartup();
  }
})();
