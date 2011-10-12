function updateProgressBar(percent, message, styleBackground,
                           styleForeground, styleText) {
    console.log("message: " + message);
    console.log("progress: " + percent + "%");
    var progress_bar =
        document.getElementById('progress_bar');
    var ctx = progress_bar.getContext('2d');
    var width = progress_bar.width;
    ctx.fillStyle = styleForeground ? styleForeground :
        "rgb(200, 200, 200)";
    ctx.fillRect(0, 0, percent * width, 20);
    ctx.fillStyle = styleBackground ? styleBackground :
        "#A9A9A9";  // From the css
    ctx.fillRect(percent * width, 0, width, 20);
    ctx.fillStyle = styleText ? styleText : "black";
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    ctx.font = 'sans-serif';
    ctx.fillText(message, width / 2, 10, 3 * width / 4);
}

function HandleProgress(event) {
    var loadPercent = 0.0;
    var loadPercentString;
    if (event.lengthComputable && event.total > 0) {
        loadPercent = event.loaded / event.total;
    } else {
        // The total length is not yet known.
        loadPercent = -1.0;
    }
    updateProgressBar(loadPercent, "Downloading Wesnoth executable...");
}

function HandleMessage(event) {
    console.log(event.data);
    var data = JSON.parse(event.data);
    updateProgressBar(data[1] / data[2], data[0]);
}

quota_required = 380*1024*1024;
window.webkitStorageInfo.requestQuota(PERSISTENT, quota_required, function(bytes) {
        console.log("Persistent storage quota granted: " + bytes + " bytes");
        if (bytes >= quota_required) {
            var embed = document.createElement('embed');
            embed.setAttribute('name', 'nacl_module');
            embed.setAttribute('id', 'wesnoth');
            embed.setAttribute('width', 1024);
            embed.setAttribute('height', 800);
            embed.setAttribute('src', 'wesnoth.nmf');
            embed.setAttribute('type', 'application/x-nacl');
            var div = document.getElementById("nacl_div");
            div.appendChild(embed);
            div.addEventListener('progress', HandleProgress, true);
            div.addEventListener('message', HandleMessage, true);
        } else {
            console.log("Not enough space.");
        }
    }, function(e) {
        console.log("Quota request error: " + e);
    });
