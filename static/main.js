function send_action(action) {fetch('/set?action='+action.value)}
function send_volume(volume) {fetch('/set?volume='+volume.value)}

function set_ui(data){
    document.main.volume.value = data.volume;
    document.main.state.value  = data.input;   
};

document.addEventListener('DOMContentLoaded', () => {
    fetch('/get')
    .then((response) => response.json())
    .then((data) => set_ui(data));
}, false);

const evtSource = new EventSource("/update");
evtSource.addEventListener("update_ui", (event) => {set_ui(JSON.parse(event.data));});

