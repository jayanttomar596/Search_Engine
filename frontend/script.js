const input = document.getElementById("query");
const output = document.getElementById("output");
const suggestions = document.getElementById("suggestions");
const uploadStatus = document.getElementById("uploadStatus");

// ========================
// 🔹 Upload file
// ========================
function uploadFile() {
  const fileInput = document.getElementById("fileInput");
  const file = fileInput.files[0];

  if (!file) {
    uploadStatus.innerText = "Please select a file.";
    uploadStatus.style.color = "red";
    return;
  }

  const reader = new FileReader();

  reader.onload = function (e) {
    fetch("http://localhost:8080/upload?filename=" + encodeURIComponent(file.name), {
      method: "POST",
      headers: {
        "Content-Type": "text/plain"
      },
      body: e.target.result
    })
    .then(res => {
      if (!res.ok) {
        throw new Error("Server error");
      }
      return res.text();
    })
    .then(msg => {
      uploadStatus.innerText = msg;
      uploadStatus.style.color = "#22c55e";

      fileInput.value = "";
      updateCorpusInfo();
    })
    .catch(err => {
      console.error("Upload error:", err);
      uploadStatus.innerText = "Upload failed!";
      uploadStatus.style.color = "red";
    });
  };

  reader.onerror = function () {
    uploadStatus.innerText = "Failed to read file.";
    uploadStatus.style.color = "red";
  };

  reader.readAsText(file);
}




// ========================
// 🔹 WORD BASED HIGHLIGHT
// ========================
function highlightSnippet(text, query) {

  let words = query.toLowerCase().split(" ");

  words.forEach(word => {

    if (!word) return;

    const regex = new RegExp(`\\b(${word})\\b`, "gi");

    text = text.replace(regex, match => `<mark>${match}</mark>`);
  });

  return text;
}



// ========================
// 🔹 Search
// ========================
function search() {
  const q = input.value.trim();
  if (!q) return;

  fetch(`http://localhost:8080/search?q=${encodeURIComponent(q)}`)
    .then(res => {
      if (!res.ok) {
        throw new Error("Search request failed");
      }
      return res.json();
    })
    .then(data => {
      output.innerHTML = `
      <p style="color:#94a3b8">
        Search time: <b>${data.latency_ms.toFixed(3)} ms</b>
      </p>
    `;

      if (!data.results || data.results.length === 0) {
        output.innerHTML = "<p class='no-result'>No results found</p>";
        return;
      }

      if(data.results[0].suggestion){
        output.innerHTML += `
          <p style="color:#facc15">
            Did you mean:
            <b onclick="query.value='${data.results[0].suggestion}'; search()">
              ${data.results[0].suggestion}
            </b> ?
          </p>
        `;
      }


      data.results.forEach(r => {
        const card = document.createElement("div");
        card.className = "result-card";

        const highlighted = highlightSnippet(r.snippet, input.value);

        card.innerHTML = `
          <h3>${r.document}</h3>
          <p><b>Frequency:</b> ${r.frequency}</p>
          <p><b>Score:</b> ${r.score.toFixed(4)}</p>
          <p class="snippet">${highlighted}...</p>
        `;
        
        output.appendChild(card);
      });
    })
    .catch(err => {
      console.error("Search error:", err);
      output.innerHTML = "<p class='no-result'>Search failed</p>";
    });
}


// ========================
// 🔹 Autocomplete
// ========================
input.addEventListener("input", () => {
  const q = input.value.trim();

  if (!q) {
    suggestions.innerHTML = "";
    return;
  }

  fetch(`http://localhost:8080/autocomplete?prefix=${encodeURIComponent(q)}`)
    .then(res => res.json())
    .then(data => {
      suggestions.innerHTML = "";

      if (!data.suggestions) return;

      data.suggestions.slice(0, 5).forEach(word => {
        const div = document.createElement("div");
        div.className = "suggestion";
        div.innerText = word;

        div.onclick = () => {
          input.value = word;
          suggestions.innerHTML = "";
        };

        suggestions.appendChild(div);
      });
    })
    .catch(err => {
      console.error("Autocomplete error:", err);
    });
});





function updateCorpusInfo() {
  fetch("http://localhost:8080/corpusInfo")
    .then(res => res.json())
    .then(data => {
      document.getElementById("corpusStats").innerText =
        `Documents: ${data.documents} | Vocabulary: ${data.vocabulary}`;
    });
}



function loadInitialCorpus() {

  if (!confirm("This will replace the current corpus. Continue?")) {
    return;
  }

  fetch("http://localhost:8080/loadSample")
    .then(res => res.json())
    .then(data => {

      document.getElementById("indexStats").innerText =
        `Index built in ${data.indexing_time_ms.toFixed(3)} ms using ${data.threads_used} threads`;

      updateCorpusInfo();
    })
    .catch(err => {
      console.error("Load error:", err);
    });
}



function rebuildIndex() {
  fetch("http://localhost:8080/rebuildIndex", { method: "POST" })
    .then(res => res.json())
    .then(data => {

      document.getElementById("indexStats").innerText =
        `Index rebuilt in ${data.indexing_time_ms.toFixed(3)} ms using ${data.threads_used} threads`;

      updateCorpusInfo();
    });
}



function clearCorpus() {
  fetch("http://localhost:8080/clearCorpus", { method: "POST" })
    .then(res => res.text())
    .then(msg => {
      alert(msg);
      document.querySelector("button[onclick='loadInitialCorpus()']").disabled = false;
      updateCorpusInfo();
    });
}





function runBenchmark() {

  fetch("http://localhost:8080/benchmark")
    .then(res => res.json())
    .then(data => {

      document.getElementById("indexStats").innerText =
        `Single-thread: ${data.single_thread_ms.toFixed(3)} ms | ` +
        `Multi-thread: ${data.multi_thread_ms.toFixed(3)} ms | ` +
        `Threads: ${data.threads_used} | ` +
        `Speedup: ${data.speedup.toFixed(2)}x`;
    })
    .catch(err => {
      console.error("Benchmark error:", err);
    });
}


window.onload = updateCorpusInfo;



