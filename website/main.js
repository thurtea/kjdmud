(function () {
  var path = (window.location.pathname.split("/").pop() || "index.html").toLowerCase();
  if (!path || path === "/") path = "index.html";
  document.querySelectorAll(".nav a[href]").forEach(function (link) {
    var href = (link.getAttribute("href") || "").toLowerCase();
    if (href === path || (path === "index.html" && href === "index.html")) {
      link.setAttribute("aria-current", "page");
    }
  });
})();
