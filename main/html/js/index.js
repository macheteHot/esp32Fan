let fanOn = false;
let fanLevel = 1;

dayjs.extend(window.dayjs_plugin_duration);

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const fanSvgBtn = document.querySelector(".fan-svg");
const fanBlades = document.querySelector(".fan-blades");
const timerOff = document.querySelector(".timer-off");
const setTimerBtn = document.getElementById("set-timer-btn");
const durationPickerDom = document.querySelector(".duration-picker");
const radioGroup = document.querySelector(".radio-group");
const fanSpeedRadios = document.querySelectorAll('input[name="fan-speed"]');

function checkedLevel() {
  fanSpeedRadios.forEach((radio) => {
    if (radio.value === fanLevel.toString()) {
      radio.checked = true;
    }
  });
}

radioGroup.addEventListener("change", (event) => {
  const target = event.target;
  if (target.tagName === "INPUT" && target.type === "radio") {
    const level = target.value;
    fanLevel = parseInt(level, 10);
    fanOn = true; // 确保风扇处于开启状态
    changeAnimation(true);
    checkedLevel();
    // 发送请求更新风扇速度
    fetch(`/api/on?level=${level}`);
  }
});

const timePicker = new Picker(durationPickerDom, {
  format: "HH:mm",
  date: dayjs().hour(0).minute(30).second(0).toDate(),
  text: {
    title: "设置关闭倒计时",
    cancel: "取消",
    confirm: "确定",
  },
  translate(type, text) {
    const suffixes = {
      hour: "时",
      minute: "分",
    };
    return Number(text) + suffixes[type];
  },
  pick() {
    const time = dayjs(timePicker.getDate());
    const hour = time.hour();
    const minute = time.minute();
    const duration = dayjs.duration({
      hours: hour,
      minutes: minute,
    });
    const seconds = duration.asSeconds();
    fetch(`/api/timer_off?seconds=${seconds}`);
  },
});

function changeAnimation(isOn) {
  const animationDuration = 1.5 - (fanLevel - 1) * 0.5 + "s"; // 1档1.5s, 2档1s, 3档0.5s
  if (isOn) {
    // 只有在没动画时才设置动画，避免重新开始
    if (!fanBlades.style.animation) {
      fanBlades.style.animation = "spin linear infinite";
      fanBlades.style.transform = "";
      fanSvgBtn.style.filter = "none";
    }
    fanBlades.style.animationDuration = animationDuration; // 实时调整速度
  } else {
    // 停止动画
    fanBlades.style.animation = "";
    const computedStyle = window.getComputedStyle(fanBlades);
    const matrix = new DOMMatrixReadOnly(computedStyle.transform);
    const angle = Math.round(Math.atan2(matrix.b, matrix.a) * (180 / Math.PI));
    const current = (angle + 360) % 360;

    const targetAngle = current + fanLevel * 120;
    const stopAngle = Math.round(targetAngle / 120) * 120;

    fanBlades.style.transition = "transform 1s ease-out";
    fanBlades.style.transform = `rotate(${stopAngle}deg)`;

    setTimeout(() => {
      fanBlades.style.transition = "none";
      fanBlades.style.transform = `rotate(0deg)`; // 可选：统一状态
    }, 1100);

    fanSvgBtn.style.filter = "grayscale(100%)";
  }
}

async function toggleFan() {
  navigator?.vibrate?.(200);
  fetch(`/api/${fanOn ? "off" : "on"}`);
  fanOn = !fanOn;
  changeAnimation(fanOn);
  checkedLevel();
}

function cancelTimer() {
  fetch("/api/cancel_timer");
}

fanSvgBtn.addEventListener("click", toggleFan);

setTimerBtn.addEventListener("click", () => {
  timePicker.show();
});

timerOff.addEventListener("click", cancelTimer);

async function updateStatus() {
  try {
    const response = await fetch(`/api/status`);
    const { status, timer_left, last_fan_level } = await response.json();
    fanOn = status === 1;
    fanLevel = last_fan_level || 1;
    checkedLevel();
    changeAnimation(fanOn);

    // 显示关闭倒计时
    if (typeof timer_left === "number" && timer_left > 0) {
      const duration = dayjs.duration(timer_left, "seconds");
      const timeStr = duration.format("HH:mm:ss");
      timerOff.style.visibility = "visible";
      timerOff.textContent = `取消倒计时：${timeStr}`;
    } else {
      timerOff.style.visibility = "hidden";
    }
  } catch (error) {
    console.error("Error fetching status:", error);
  }
  await sleep(5000); // 每5秒更新一次状态
  updateStatus();
}

updateStatus();
