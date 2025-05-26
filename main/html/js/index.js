let fanOn = false;

dayjs.extend(window.dayjs_plugin_duration);

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const fanSvgBtn = document.querySelector(".fan-svg");
const fanBlades = document.querySelector(".fan-blades");
const timerOff = document.querySelector(".timer-off");
const setTimerBtn = document.getElementById("set-timer-btn");
const durationPickerDom = document.querySelector(".duration-picker");

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
  fanBlades.style.animationPlayState = isOn ? "running" : "paused";
  fanSvgBtn.style.filter = isOn ? "none" : "grayscale(100%)";
}

async function toggleFan() {
  navigator?.vibrate?.(200);
  fetch(`/api/${fanOn ? "off" : "on"}`);
  fanOn = !fanOn;
  changeAnimation(fanOn);
}

async function updateStatus() {
  try {
    const response = await fetch("/api/status");
    const { status, timer_left } = await response.json();
    fanOn = status === 1;
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
  // 每隔1秒更新一次状态
  await sleep(1000);
  updateStatus();
}

function cancelTimer() {
  fetch("/api/cancel_timer");
}

fanSvgBtn.addEventListener("click", toggleFan);

setTimerBtn.addEventListener("click", () => {
  timePicker.show();
});

timerOff.addEventListener("click", cancelTimer);

updateStatus();
