// Load before the stylesheet to restore appearance without a light-theme flash.
(() => {
  const key = "microcs-flasher-theme";
  const normalize = (value) =>
    ["light", "dark"].includes(value) ? value : "system";
  let preference = "system";
  try {
    preference = normalize(localStorage.getItem(key));
  } catch {
    /* Storage can be disabled. */
  }
  function apply(value) {
    preference = normalize(value);
    if (preference === "system") delete document.documentElement.dataset.theme;
    else document.documentElement.dataset.theme = preference;
    const select = document.getElementById("theme");
    if (select) select.value = preference;
  }
  apply(preference);
  document.addEventListener("DOMContentLoaded", () => {
    const select = document.getElementById("theme");
    if (!select) return;
    select.value = preference;
    select.addEventListener("change", () => {
      apply(select.value);
      try {
        if (preference === "system") localStorage.removeItem(key);
        else localStorage.setItem(key, preference);
      } catch {
        /* Appearance still works for this visit when storage is blocked. */
      }
    });
  });
  window.addEventListener("storage", (event) => {
    if (event.key === key || event.key === null) apply(event.newValue);
  });
})();
