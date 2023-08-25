(function ($) {
     // Send GET request to fetch user info JSON
     $.get("/userinfo", function (data) {
         // Assuming the JSON has a "username" field
         var username = data.username;
         $("#username").text("用户名：" + username); // Display username in the paragraph
     });
 })(jQuery); 