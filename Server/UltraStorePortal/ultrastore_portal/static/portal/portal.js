/* The portal's own script: what the pages do without loading again. No
   build and no framework - Bootstrap's bundle (vendor/bootstrap) for the
   navigation and the dialog, Dropzone (vendor/dropzone) for the upload areas,
   and this file, which every page loads last.

   Everything here is an addition to a page that works without it: a field
   that is hidden is still checked by the server, a form sent in place has an
   address that answers a plain browser as well.

   - Fields that depend on another field: a form with data-depends shows a
     field only while its controller has one of the rule's values
     (web/forms.py, DependentFieldsMixin); one with data-choices offers a
     select's options only for the system chosen (PackageFieldsForm).
   - Forms sent in place: a form with data-in-place is sent by this script
     (web/console.py); the answer names the parts of the page to draw again,
     or the errors, which are shown at their fields - the file chosen stays
     chosen. A file's upload fills the form's progress bar.
   - The dialog: a link with data-dialog opens its address in the page's
     dialog (a package's edit form).
   - Upload areas: an element with data-upload takes files dropped on it or
     picked through it and uploads each at once.

   Version: 0.3.0 */
(function () {
  "use strict";

  function all(root, selector) {
    return Array.prototype.slice.call(root.querySelectorAll(selector));
  }

  function element(tag, className, text) {
    var node = document.createElement(tag);
    if (className) node.className = className;
    if (text) node.textContent = text;
    return node;
  }

  // ---------------------------------------------------------------- fields

  /* What a field holds, as the rules name it: a ticked box is "on". */
  function valueOf(form, name) {
    var control = form.elements[name];
    if (!control) return "";
    if (control.type === "checkbox") return control.checked ? "on" : "";
    return control.value || "";
  }

  function applyDepends(form, rules) {
    var hidden = {};
    rules.forEach(function (rule) {
      var box = form.querySelector('[data-field="' + rule.field + '"]');
      if (!box) return;
      var shown = !hidden[rule.on] && rule.values.indexOf(valueOf(form, rule.on)) !== -1;
      box.hidden = !shown;
      if (!shown) hidden[rule.field] = true;
    });
  }

  function applyChoices(form, rules) {
    rules.forEach(function (rule) {
      var select = form.elements[rule.field], on = valueOf(form, rule.on);
      if (!select || !select.options) return;
      all(select, "option").forEach(function (option) {
        if (!option.value) return;
        var fits = !on || ((!rule.of[on] || rule.of[on].indexOf(option.value) !== -1) &&
                           (!rule.only[option.value] || rule.only[option.value].indexOf(on) !== -1));
        option.hidden = option.disabled = !fits;
      });
      var chosen = select.options[select.selectedIndex];
      if (chosen && chosen.disabled) select.value = "";
    });
  }

  function refreshFields(form) {
    if (form.dataset.depends) applyDepends(form, JSON.parse(form.dataset.depends));
    if (form.dataset.choices) applyChoices(form, JSON.parse(form.dataset.choices));
  }

  /* The forms under `root` whose fields depend on each other, as they are now. */
  function setUpFields(root) {
    all(root, "form[data-depends], form[data-choices]").forEach(refreshFields);
  }

  document.addEventListener("change", function (event) {
    var form = event.target.form;
    if (form && (form.dataset.depends || form.dataset.choices)) refreshFields(form);
  });
  document.addEventListener("reset", function (event) {
    var form = event.target;
    window.setTimeout(function () { refreshFields(form); }, 0);
  });

  // ------------------------------------------------------------- in place

  function applyFragments(fragments) {
    Object.keys(fragments || {}).forEach(function (selector) {
      var target = document.querySelector(selector);
      if (!target) return;
      target.innerHTML = fragments[selector];
      setUpFields(target);
    });
    // The screenshot area goes once the listing has all the pictures it shows.
    var thumbs = document.querySelector("#shots .thumbs"), area = document.getElementById("shot-area");
    if (thumbs && area) area.classList.toggle("is-full", Number(thumbs.dataset.count) >= Number(thumbs.dataset.max));
  }

  function clearErrors(form) {
    all(form, ".is-invalid").forEach(function (control) { control.classList.remove("is-invalid"); });
    all(form, ".invalid-feedback").forEach(function (note) { note.remove(); });
    var box = form.querySelector("[data-form-error]");
    if (box) { box.hidden = true; box.textContent = ""; }
  }

  /* Errors by field name; those of no field (or of a field the form does not
     show) go to the form's own line, or to an alert where it has none. */
  function showErrors(form, errors) {
    var general = [];
    Object.keys(errors).forEach(function (name) {
      var control = name ? form.elements[name] : null;
      if (!control || !control.classList) { general = general.concat(errors[name]); return; }
      control.classList.add("is-invalid");
      errors[name].forEach(function (message) {
        control.insertAdjacentElement("afterend", element("div", "invalid-feedback d-block", message));
      });
    });
    if (!general.length) {
      var first = form.querySelector(".is-invalid");
      if (first) first.focus();
      return;
    }
    var box = form.querySelector("[data-form-error]");
    if (!box) { window.alert(general.join("\n")); return; }
    box.textContent = general.join(" ");
    box.hidden = false;
    box.scrollIntoView({ block: "nearest" });
  }

  /* What to say when the answer is not the portal's JSON: the web server in
     front refused the size, the session ended, or something broke. */
  function plainFailure(xhr) {
    if (xhr.status === 413) return "The file is larger than the server takes.";
    if (xhr.responseURL && xhr.responseURL.indexOf("/account/signin") !== -1) return "You are signed out. Sign in again in another tab, then send this once more.";
    if (xhr.status === 403) return "The page is too old to send this. Reload it and try again.";
    return "The server could not take this (" + (xhr.status || "no answer") + "). Try again.";
  }

  function sendInPlace(form) {
    if (form.dataset.busy) return;
    clearErrors(form);
    var xhr = new XMLHttpRequest(), buttons = all(form, "button");
    var bar = form.querySelector("[data-progress]");
    var uploads = all(form, "input[type=file]").some(function (input) { return input.files && input.files.length; });
    var body = new FormData(form);

    function finish() {
      delete form.dataset.busy;
      buttons.forEach(function (button) { button.disabled = false; });
      if (bar) { bar.hidden = true; bar.firstElementChild.style.width = "0"; }
    }

    if (bar && uploads) {
      bar.hidden = false;
      xhr.upload.onprogress = function (event) {
        if (!event.lengthComputable) return;
        var percent = Math.round(100 * event.loaded / event.total);
        bar.firstElementChild.style.width = percent + "%";
        bar.firstElementChild.textContent = percent < 100 ? percent + "%" : "Checking the archive...";
        bar.setAttribute("aria-valuenow", percent);
      };
    }
    xhr.onload = function () {
      var data = null;
      try { data = JSON.parse(xhr.responseText); } catch (error) { data = null; }
      finish();
      if (!data || typeof data.ok !== "boolean") { showErrors(form, { "": [plainFailure(xhr)] }); return; }
      if (!data.ok) {
        var errors = data.errors || {};
        if (data.error) errors[""] = (errors[""] || []).concat([data.error]);
        showErrors(form, errors);
        return;
      }
      var dialog = form.closest(".modal");
      applyFragments(data.fragments);
      if (data.reset) form.reset();
      if (data.close && dialog && window.bootstrap) window.bootstrap.Modal.getOrCreateInstance(dialog).hide();
    };
    xhr.onerror = function () { finish(); showErrors(form, { "": ["The server could not be reached. Try again."] }); };
    xhr.open("POST", form.action);
    xhr.setRequestHeader("X-Requested-With", "XMLHttpRequest");
    xhr.setRequestHeader("Accept", "application/json");
    form.dataset.busy = "1";
    buttons.forEach(function (button) { button.disabled = true; });
    xhr.send(body);
  }

  document.addEventListener("submit", function (event) {
    var form = event.target;
    if (!form.matches || !form.matches("form[data-in-place]")) return;
    event.preventDefault();
    if (form.dataset.confirm && !window.confirm(form.dataset.confirm)) return;
    sendInPlace(form);
  });

  // ---------------------------------------------------------------- dialog

  document.addEventListener("click", function (event) {
    var link = event.target.closest ? event.target.closest("a[data-dialog]") : null;
    var dialog = document.getElementById("dialog");
    if (!link || !dialog || !window.bootstrap || !window.fetch) return;
    event.preventDefault();
    window.fetch(link.href, { headers: { "X-Requested-With": "XMLHttpRequest" }, credentials: "same-origin" })
      .then(function (response) {
        if (!response.ok || response.redirected) throw new Error("not the form");
        return response.text();
      })
      .then(function (html) {
        var content = dialog.querySelector(".modal-content");
        content.innerHTML = html;
        setUpFields(content);
        window.bootstrap.Modal.getOrCreateInstance(dialog).show();
      })
      .catch(function () { window.location.href = link.href; });   // the page of its own says why
  });
  document.addEventListener("shown.bs.modal", function (event) {
    var first = event.target.querySelector("select, input:not([type=hidden]), textarea");
    if (first) first.focus();
  });

  // --------------------------------------------------------------- uploads

  function queueRow(queue, name) {
    var row = element("div", "d-flex align-items-center gap-2 small");
    var label = element("span", "text-truncate", name);
    label.style.maxWidth = "40%";
    var progress = element("div", "progress flex-grow-1");
    progress.setAttribute("role", "progressbar");
    progress.setAttribute("aria-label", "Uploading " + name);
    progress.appendChild(element("div", "progress-bar"));
    row.appendChild(label);
    row.appendChild(progress);
    queue.appendChild(row);
    return row;
  }

  function queueFailure(row, name, message) {
    var note = element("div", "alert alert-danger alert-dismissible py-2 mb-0 small");
    note.setAttribute("role", "alert");
    note.appendChild(element("b", "", name + ": "));
    note.appendChild(document.createTextNode(message));
    var close = element("button", "btn-close");
    close.type = "button";
    close.setAttribute("aria-label", "Dismiss");
    close.style.padding = ".75rem";
    close.addEventListener("click", function () { note.remove(); });
    note.appendChild(close);
    row.replaceWith(note);
  }

  function setUpUploads() {
    var areas = all(document, "[data-upload]");
    if (!areas.length || !window.Dropzone) return;   // the plain forms stay
    all(document, "form[data-without-script]").forEach(function (form) { form.remove(); });
    areas.forEach(function (area) {
      var queue = document.querySelector(area.dataset.queue);
      var single = !("multiple" in area.dataset);
      var zone = new window.Dropzone(area, {
        url: area.dataset.upload,
        paramName: area.dataset.param,
        acceptedFiles: area.dataset.accept,
        maxFilesize: Number(area.dataset.maxMb),
        maxFiles: single ? 1 : null,
        parallelUploads: 1,              // one after another: each screenshot takes the next position
        disablePreviews: true,           // the page draws the pictures itself, from the server's answer
        createImageThumbnails: false,
        headers: { "X-CSRFToken": area.dataset.csrf, "X-Requested-With": "XMLHttpRequest" },
        dictFileTooBig: "The picture is {{filesize}} MB; a picture is at most {{maxFilesize}} MB.",
        dictInvalidFileType: "A picture is a PNG, a JPEG or a WebP file.",
        dictMaxFilesExceeded: "One picture at a time here.",
        dictResponseError: "The server could not take the picture ({{statusCode}}). Try again."
      });
      area.hidden = false;
      zone.on("addedfile", function (file) { file.row = queueRow(queue, file.name); });
      zone.on("uploadprogress", function (file, progress) {
        if (file.row) file.row.querySelector(".progress-bar").style.width = Math.round(progress) + "%";
      });
      zone.on("success", function (file, data) {
        if (file.row) file.row.remove();
        if (data && data.fragments) applyFragments(data.fragments);
      });
      zone.on("error", function (file, message) {
        var text = typeof message === "string" ? message : (message && message.error) || "The upload failed. Try again.";
        if (file.row) queueFailure(file.row, file.name, text);
      });
      zone.on("complete", function (file) { zone.removeFile(file); });   // the area is ready for the next one
      area.addEventListener("keydown", function (event) {
        if ((event.key === "Enter" || event.key === " ") && zone.hiddenFileInput) { event.preventDefault(); zone.hiddenFileInput.click(); }
      });
    });
    // A picture dropped beside an area is not opened in the page's place.
    ["dragover", "drop"].forEach(function (name) {
      window.addEventListener(name, function (event) {
        if (!(event.target.closest && event.target.closest("[data-upload]"))) event.preventDefault();
      });
    });
  }

  // "Not this one again" on an advertisement: remembered in this browser
  // and nowhere else. The store is not told, so it cannot know who hid what;
  // it is this page that leaves the card out.
  function setUpAds() {
    var KEY = "promote.hidden", hidden;
    try { hidden = JSON.parse(window.localStorage.getItem(KEY) || "[]"); } catch (error) { return; }
    if (!Array.isArray(hidden)) hidden = [];
    function leaveOut(card) {
      var row = card.closest(".col") || card, section = row.closest(".ad-row");
      row.remove();
      if (section && !section.querySelector("[data-ad]")) section.remove();   // a row of advertisements with none left
    }
    document.querySelectorAll("[data-ad]").forEach(function (card) {
      if (hidden.indexOf(card.getAttribute("data-ad")) !== -1) { leaveOut(card); return; }
      var button = card.querySelector("[data-ad-hide]");
      if (!button) return;
      button.hidden = false;
      button.addEventListener("click", function () {
        hidden.push(card.getAttribute("data-ad"));
        try { window.localStorage.setItem(KEY, JSON.stringify(hidden.slice(-200))); } catch (error) { /* a browser that keeps nothing */ }
        leaveOut(card);
      });
    });
  }

  // "Copy" beside a field: data-copy names the field, data-copied what the
  // button says for a moment afterwards.
  function setUpCopy() {
    document.querySelectorAll("[data-copy]").forEach(function (button) {
      var field = document.querySelector(button.getAttribute("data-copy"));
      if (!field) return;
      button.addEventListener("click", function () {
        var said = button.textContent;
        function done() {
          button.textContent = button.getAttribute("data-copied") || said;
          window.setTimeout(function () { button.textContent = said; }, 1500);
        }
        if (navigator.clipboard && navigator.clipboard.writeText) {
          navigator.clipboard.writeText(field.value).then(done, function () { field.select(); });
        } else {
          field.select();
          try { if (document.execCommand("copy")) done(); } catch (error) { /* the field stays selected: Ctrl+C */ }
        }
      });
    });
  }

  setUpFields(document);
  setUpUploads();
  setUpAds();
  setUpCopy();
})();
