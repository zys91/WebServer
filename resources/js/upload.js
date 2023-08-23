document.addEventListener("DOMContentLoaded", function () {
     const uploadForm = document.getElementById("upload-form");
     const fileInput = document.getElementById("file-input");
     const fileListUl = document.getElementById("file-list-ul");
     const uploadBtn = document.getElementById("upload-btn");

     fileInput.addEventListener("change", function () {
          uploadBtn.disabled = !fileInput.value; // Enable the upload button if a file is selected
     });

     uploadForm.addEventListener("submit", async function (e) {
          e.preventDefault();
          if (!fileInput.files[0]) {
               alert("请先选择文件！");
               return;
          }

          // Get the selected file and its name
          const selectedFile = fileInput.files[0];
          const selectedFilename = selectedFile.name;

          // Check if the selected filename already exists
          if (checkIfFileExists(selectedFilename)) {
               alert("同名文件已存在，请重新选择文件！");
               return;
          }

          uploadBtn.disabled = true; // Disable the upload button while uploading

          const formData = new FormData();
          formData.append("file", fileInput.files[0]);

          try {
               const response = await fetch("/upload", {
                    method: "POST",
                    body: formData,
               });

               if (response.ok) {
                    const fileInfo = await response.json();
                    if (fileInfo.err !== 0) {
                         console.error("err:", fileInfo.err);
                         return;
                    }
                    const listItem = document.createElement("li");
                    listItem.innerHTML = `
                    <li class="file-item">
                         <div class="file-name">${truncateFileName(fileInfo.fileName, 60)}</div>
                         <div class="file-details">
                              <span class="file-size">${formatFileSize(fileInfo.fileSize)}</span>
                              <span class="upload-date">${formatDateTime(fileInfo.uploadDate)}</span>
                         </div>
                         <div class="file-actions">
                              <button class="btn btn-link btn-delete" data-file="${fileInfo.fileName}">删除</button>
                              <a class="btn btn-link btn-download" href="/download?file=${fileInfo.fileName}" target="_blank">下载</a>
                         </div>
                    </li>
                    `;
                    fileListUl.appendChild(listItem);
                    fileInput.value = ""; // Clear the file input
               } else {
                    console.error("File upload failed.");
               }
          } catch (error) {
               console.error("Error uploading file:", error);
          }

          uploadBtn.disabled = false; // Re-enable the upload button
     });

     fileListUl.addEventListener("click", async function (e) {
          if (e.target.classList.contains("btn-delete")) {
               const fileName = e.target.getAttribute("data-file");

               // Show a confirmation dialog before proceeding
               const confirmed = confirm(`确定需要删除文件"${fileName}"?`);
               if (!confirmed) {
                    return;
               }

               try {
                    const response = await fetch("/delete", {
                         method: "POST",
                         headers: {
                              "Content-Type": "application/json",
                         },
                         body: JSON.stringify({ file: fileName }),
                    });

                    if (response.ok) {
                         const responseData = await response.json();
                         if (responseData.err === 0) {
                              // Handle success
                              e.target.parentElement.parentElement.remove();
                         } else {
                              console.error("err:", responseData.err);

                         }
                    } else {
                         console.error("File deletion failed.");
                    }
               } catch (error) {
                    console.error("Error deleting file:", error);
               }
          }
     });

     // Fetch and display the list of uploaded files
     async function fetchFileList() {
          try {
               const response = await fetch("/fileslist");

               if (response.ok) {
                    const fileData = await response.json();

                    if (Array.isArray(fileData) && fileData.length > 0) {
                         fileData.forEach(fileInfo => {
                              const { fileName, fileSize, uploadDate } = fileInfo;
                              const listItem = document.createElement("li");
                              listItem.innerHTML = `
                              <li class="file-item">
                                   <div class="file-name">${truncateFileName(fileName, 60)}</div>
                                   <div class="file-details">
                                        <span class="file-size">${formatFileSize(fileSize)}</span>
                                        <span class="upload-date">${formatDateTime(uploadDate)}</span>
                                   </div>
                                   <div class="file-actions">
                                        <button class="btn btn-link btn-delete" data-file="${fileName}">删除</button>
                                        <a class="btn btn-link btn-download" href="/download?file=${fileName}" target="_blank">下载</a>
                                   </div>
                              </li>
                              `;
                              fileListUl.appendChild(listItem);
                         });
                    }
               }
          } catch (error) {
               console.error("Error fetching file list:", error);
          }
     }

     function checkIfFileExists(filename) {
          const fileListUl = document.getElementById("file-list-ul");
          const fileItems = fileListUl.querySelectorAll("li");

          for (const fileItem of fileItems) {
               const listItemText = fileItem.textContent.trim();
               const newlineIndex = listItemText.indexOf('\n');
               if (newlineIndex !== -1) {
                    const result = listItemText.substring(0, newlineIndex);
                    if (result === filename) {
                         return true; // Found a file with the same name
                    }
               }
          }

          return false; // No file with the same name found
     }

     // Format file size in a human-readable format
     function formatFileSize(size) {
          if (size < 1024) {
               return `${size} B`;
          } else if (size < 1024 * 1024) {
               return `${(size / 1024).toFixed(2)} KB`;
          } else {
               return `${(size / (1024 * 1024)).toFixed(2)} MB`;
          }
     }

     // Format date as YYYY-MM-DD HH:MM:SS
     function formatDateTime(unixTimestamp) {
          const date = new Date(unixTimestamp * 1000);
          const year = date.getFullYear();
          const month = String(date.getMonth() + 1).padStart(2, "0");
          const day = String(date.getDate()).padStart(2, "0");
          const hours = String(date.getHours()).padStart(2, "0");
          const minutes = String(date.getMinutes()).padStart(2, "0");
          const seconds = String(date.getSeconds()).padStart(2, "0");

          return `${year}-${month}-${day} ${hours}:${minutes}:${seconds}`;
     }

     // Format file name if it's too long
     function truncateFileName(name, maxLength) {
          if (name.length > maxLength) {
               return name.substr(0, maxLength - 3) + '...';
          }
          return name;
     }



     fetchFileList();
});
