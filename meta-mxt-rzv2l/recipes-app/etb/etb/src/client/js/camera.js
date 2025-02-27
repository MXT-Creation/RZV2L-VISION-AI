let predictionData = {
	type: null,
	data: null
};

let predictionImage = null; // FIXME hack

object_colors = ['orange', 'magenta', 'red', 'yellow', 'green', 'blue'];
object_color_idx = 0;
object_color_map = new Map();

function camera_device_play_toggle_button(ws, buttonElement) {
	var sel = document.getElementById("camera_device_sel");
	var play = (buttonElement.value == "Play");

	const msg_json = {
		"name": play ? "camera-device-play" : "camera-device-stop",
		"value": { "device": sel.value },
	};

	sel.disabled = play;

	ws.send(JSON.stringify(msg_json));

	// FIXME: bind this to server response
	buttonElement.value = play ? "Stop" : "Play";
}

function camera_device_play_toggle(ws, ev) {
	camera_device_play_toggle_button(ws, ev.currentTarget);
}

function camera_device_selection_change(ev) {
	var play = document.getElementById("camera_device_play");
	play.disabled = (ev.currentTarget.value == "");
}

function camera_devices_get_request(ws) {
	const msg_json = { "name": "camera-devices-get" };
	ws.send(JSON.stringify(msg_json));
}

function camera_devices_get_response(ws, msg) {
	var sel = document.getElementById("camera_device_sel");
	var play = document.getElementById("camera_device_play");

	play.disabled = true;

	if (!Array.isArray(msg) || msg.length == 0) {
		sel.innerHTML = '<option value="" hidden>No camera device...</option>';
		return;
	}

	var devices = ["<option value='' selected>Select camera device...</option>"];
	for (let dev of msg) {
		devices.push(`<option value="${dev.device}">${dev.card}</option>`);
	}

	// Register event listener when camera device changes
	sel.innerHTML = devices.join();
	sel.addEventListener('change', camera_device_selection_change);

	// Register event listener when the play button gets pushed
	play.addEventListener('click', function (ev) {
		camera_device_play_toggle(ws, ev);
	});

	sel.selectedIndex = 1;
	camera_device_play_toggle_button(ws, play);
	play.disabled = false;
}

// adapted from: https://github.com/oatpp/example-yuv-websocket-stream/blob/master/res/cam/wsImageView.html
function yuv2CanvasImageData(canvas, data) {
	let msg_array = new Uint8ClampedArray(data);

	if (msg_array.length == 0)
		return;

	let context = canvas.getContext("2d");
	let imgData = context.createImageData(640, 480);
	let i, j;

	for (i = 0, j = 0, g = 0; i < imgData.data.length && j < msg_array.length; i += 8, j += 4, g += 2) {
		const y1 = msg_array[j];
		const u = msg_array[j + 1];
		const y2 = msg_array[j + 2];
		const v = msg_array[j + 3];

		imgData.data[i] = Math.min(255, Math.max(0, Math.floor(y1 + 1.4075 * (v - 128))));
		imgData.data[i + 1] = Math.min(255, Math.max(0, Math.floor(y1 - 0.3455 * (u - 128) - (0.7169 * (v - 128)))));
		imgData.data[i + 2] = Math.min(255, Math.max(0, Math.floor(y1 + 1.7790 * (u - 128))));
		imgData.data[i + 3] = 255;
		imgData.data[i + 4] = Math.min(255, Math.max(0, Math.floor(y2 + 1.4075 * (v - 128))));
		imgData.data[i + 5] = Math.min(255, Math.max(0, Math.floor(y2 - 0.3455 * (u - 128) - (0.7169 * (v - 128)))));
		imgData.data[i + 6] = Math.min(255, Math.max(0, Math.floor(y2 + 1.7790 * (u - 128))));
		imgData.data[i + 7] = 255;
	}
	context.putImageData(imgData, 0, 0);
}

// FIXME: hack to do this quickly
function drpai_handle_object_detection_result(ws, msg) {
	let predWindowDisplay = document.getElementById('pred_window');
	predWindowDisplay.value = "";

	if (!Array.isArray(msg) || msg.length == 0) {
		predictionData = { type: null, data: null };
		return;
	}

	predictionData = {
		type: 'object-detection',
		data: msg
	};

	let predText = "";

	msg.forEach(obj => {
		predText += `${obj.label} (${obj.probability.toFixed(2)}%) at (${obj.box.x}, ${obj.box.y} - ${obj.box.w}x${obj.box.h})\n`;
	});
	predWindowDisplay.value = predText;
}

function drpai_handle_pose_estimation_result(ws, msg) {
	let predWindowDisplay = document.getElementById('pred_window');
	predWindowDisplay.value = "";

	predictionData.type = 'pose-estimation';

	if (!Array.isArray(msg) || msg.length === 0) {
		predictionData.data = [];
	} else {
		predictionData.data = msg
	}

	let predText = "";
	msg.forEach((obj,i) => {
		predText += `No${i+1} (${obj.probability.toFixed(2)}%) at (${obj.x}, ${obj.y})\n`;
	});
	predWindowDisplay.value = predText;
}

function drpai_handle_classification_result(ws, msg) {
	let predWindowDisplay = document.getElementById('pred_window');
	predWindowDisplay.value = "";

	predictionData = {
		type: 'classification',
		data: msg
	};

	let predText = "";
	msg.forEach((obj,i) => {
		predText += `${obj.label} (${obj.probability.toFixed(2)}%)\n`;
	});
	predWindowDisplay.value = predText;
}

function connect_camera_socket() {
	let startTime = null;
	let updateElapsedTimeCounter = 0;
	const elapsedTimeFormat = { hour: "numeric", minute: "numeric", second: "numeric" };


	const callbacks = {
		"camera-devices-get": camera_devices_get_response,
		// FIXME: hack to do this quickly
		"drpai-object-detection-result": drpai_handle_object_detection_result,
		"drpai-pose-estimation-result": drpai_handle_pose_estimation_result,
		"drpai-classification-result": drpai_handle_classification_result,
	};

	function update_elapsed_time() {
		updateElapsedTimeCounter++;
		if (updateElapsedTimeCounter < 5)
			return;
		updateElapsedTimeCounter = 0;

		if (startTime == null)
			startTime = new Date();

		// VanillaJS way of formatting 00:00:00 time
		let nowTime = new Date();
		let elapsedTotal = Math.floor((nowTime - startTime) / 1000); // seconds
		let seconds = elapsedTotal % 60;
		elapsedTotal = Math.floor(elapsedTotal / 60);                // minutes
		let minutes = elapsedTotal % 60;
		let hours = Math.floor(elapsedTotal / 60);                   // hours
		let elem = document.getElementById("camera_elapsed_time");
		elem.innerHTML = hours.toString().padStart(2, '0') + ":" +
			minutes.toString().padStart(2, '0') + ":" +
			seconds.toString().padStart(2, '0');
	}

	function handle_binary_response(msg) {
		let canvas = document.getElementById("camera_canvas");
		yuv2CanvasImageData(canvas, msg.data);
		update_elapsed_time();
	}

	let imgElemCamera = document.createElement("img");
	let imgElemDrpAi = document.createElement("img");
	function handle_binary_response2(msg) {
		let id = String.fromCharCode.apply(null, new Uint8Array(msg.data, 0, 15));
		let canvas = document.getElementById("camera_canvas");
		let contextCamera = canvas.getContext("2d");

		let base64Image = btoa(String.fromCharCode.apply(null, new Uint8Array(msg.data, 16)));
		if (id.startsWith("drpai+camera"))
			predictionImage = base64Image;

		imgElemCamera.width = 640;
		imgElemCamera.height = 480;
		imgElemCamera.src = "data:image/jpeg;base64," + base64Image;

		contextCamera.drawImage(imgElemCamera, 0, 0, 640, 480);
		if (predictionImage) {
			imgElemDrpAi.width = 640;
			imgElemDrpAi.height = 480;
			imgElemDrpAi.src = "data:image/jpeg;base64," + predictionImage;

			canvas = document.getElementById("drpai_canvas");
			let contextDrpAi = canvas.getContext("2d");
			contextDrpAi.drawImage(imgElemDrpAi, 0, 0, 640, 480);

			if (predictionData) {
				switch (predictionData.type) {
					case 'object-detection':
						if (predictionData.data.length === 0)
							break;
						// Draw object detection boxes
						predictionData.data.forEach(obj => {
							let used_color = object_color_map.get(obj.label);
							if (used_color === undefined) {
								used_color = 'blue';
								if (object_color_idx < object_colors.length) {
									used_color = object_colors[object_color_idx];
									object_color_map.set(obj.label, used_color);
									object_color_idx++;
								}
							}
							contextDrpAi.strokeStyle = used_color;
							contextDrpAi.fillStyle = used_color;
							contextDrpAi.lineWidth = 8;
							contextDrpAi.strokeRect(obj.box.x, obj.box.y, obj.box.w, obj.box.h);
							contextDrpAi.font = "bold 20px sans-serif"
							contextDrpAi.fillText(obj.label, (obj.box.x + 8), (obj.box.y + 16));
						});
						break;

					case 'pose-estimation':
						// Draw pose estimation skeleton
						contextDrpAi.lineWidth = 2;
						contextDrpAi.strokeStyle = 'yellow';
						contextDrpAi.fillStyle = 'yellow';
						contextDrpAi.font = "bold 24px sans-serif"

						const ratio_w = canvas.width / 640;
						const ratio_h = canvas.height / 480;

						// Draw inference area
						contextDrpAi.strokeRect(185 * ratio_w, 0, 270 * ratio_w, 480 * ratio_h);
						contextDrpAi.fillText("Please stand here", (185 + 5) * ratio_w, (480 - 5) * ratio_h);

						if (predictionData.data.length < 17) break;

						// Draw skeleton with ratio adjustment
						const connections = [
							// Head to shoulders triangle 
							[0, 1],  // Head -> Left Shoulder
							[1, 2],  // Left Shoulder -> Right Shoulder
							[2, 0],  // Right Shoulder -> Head
							// Arms 
							[1, 3],  // Left Shoulder -> Left Elbow
							[2, 4],  // Right Shoulder -> Right Elbow
							[3, 5],  // Left Elbow -> Left Wrist
							[4, 6],  // Right Elbow -> Right Wrist
							[5, 6],  // Left Wrist -> Right Wrist
							// Torso connections 
							[5, 7],  // Left Wrist -> Left Hip
							[6, 8],  // Right Wrist -> Right Hip
							[7, 9],  // Left Hip -> Left Knee
							[8, 10], // Right Hip -> Right Knee
							// Lower body 
							[5, 11], // Left Wrist -> Left Ankle
							[6, 12], // Right Wrist -> Right Ankle
							[11, 12], // Left Ankle -> Right Ankle
							[11, 13], // Left Ankle -> Left Foot
							[12, 14], // Right Ankle -> Right Foot
							[13, 15], // Left Foot -> Left Toe
							[14, 16]  // Right Foot -> Right Toe
						];

						connections.forEach(([i, j]) => {
							let p1 = predictionData.data[i];
							let p2 = predictionData.data[j];
							if (p1 && p2 && p1.probability > 0.3 && p2.probability > 0.3) {
								contextDrpAi.beginPath();
								contextDrpAi.moveTo(p1.x * ratio_w, p1.y * ratio_h);
								contextDrpAi.lineTo(p2.x * ratio_w, p2.y * ratio_h);
								contextDrpAi.stroke();
							}
						});

						// Draw keypoints
						predictionData.data.forEach((point, i) => {
							if (point.probability > 0.3) {
								contextDrpAi.beginPath();
								contextDrpAi.arc(
									point.x * ratio_w,
									point.y * ratio_h,
									4,
									0,
									2 * Math.PI
								);
								contextDrpAi.fill();
							}
						});
						break;

					case 'classification':
						for (let i = 0; i < predictionData.data.length; i++) {
							let textSize = 24;
							contextDrpAi.fillStyle = 'red';
							contextDrpAi.font = `bold ${textSize}px sans-serif`
							// 1.25 line height
							contextDrpAi.fillText(predictionData.data[i].label, 10, 20 + textSize * 1.25 * i);
						}
						break;
				}
			}
		}

		update_elapsed_time();
	}

	function handle_json_response(msg) {
		var msg = JSON.parse(msg.data);
		if (!Object.hasOwn(msg, 'name'))
			return;
		if (!Object.hasOwn(callbacks, msg.name))
			return;
		let cb = callbacks[msg.name];
		cb(ws, Object.hasOwn(msg, "value") ? msg.value : null);
	}

	let ws = new_ws("camera");
	ws.binaryType = "arraybuffer";
	try {
		ws.onopen = function () {
			camera_devices_get_request(ws);
		};

		ws.onmessage = function got_packet(msg) {
			if (msg.data instanceof ArrayBuffer) {
				handle_binary_response2(msg);
			} else {
				handle_json_response(msg);
			}
		};

		ws.onclose = function () {
		};

	} catch (exception) {

	}
}

connect_camera_socket();

