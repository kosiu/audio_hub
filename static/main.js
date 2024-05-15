// Pure UI functions (theme + show more)
function set_initial_theme() {
    var theme = localStorage.getItem('theme');
    if (theme == null) {
        theme = window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
    }
    document.documentElement.setAttribute('data-bs-theme', theme)
    document.getElementById('theme').checked = (theme == 'dark') ? true : false;
};
//set_initial_theme();

function dark_theme(btn) {
    var theme = (btn.checked == true) ? 'dark' : 'light';
    document.documentElement.setAttribute('data-bs-theme', theme)
    localStorage.setItem('theme', theme);
};

function more(btn) {
    if (btn.checked) { btn.labels[0].innerHTML = "Pokaż mniej"; }
    else { btn.labels[0].innerHTML = "Pokaż więcej"; }
    const hidden_element = document.querySelector('.collapse')
    if ((hidden_element.clientHeight > 0 && !btn.checked) ||
        (hidden_element.clientHeight == 0 && btn.checked)) {
        new bootstrap.Collapse('#hidden_part')
    }
}

// Comunication with server functions ------------------------------

function generateRadioList(data) {
    const list_end = document.getElementById("end_radio_list");
    const parent = list_end.parentElement
    //const button_x = document.getElementById("radio_x");
    //const label_x = document.getElementById("label_x");
    data.forEach((item, index) => {
        //const button = button_x.cloneNode(true);
        //button.setAttribute('id', 'radio' + index);
        //button.setAttribute('value', index);

        //const label = label_x.cloneNode(true);
        //label.setAttribute('id', 'label' + index);
        //label.setAttribute('for', 'radio' + index);
        //label.innerHTML = `<i class="bi-broadcast-pin"></i> ${item[0]}`;
        const radioInput = document.createElement('input');
        radioInput.setAttribute('class', 'btn-check');
        radioInput.setAttribute('type', 'radio');
        radioInput.setAttribute('name', 'state');
        radioInput.setAttribute('id', 'radio' + index);
        radioInput.setAttribute('value', index);
        radioInput.setAttribute('onclick', 'send_action(this);');

        const label = document.createElement('label');
        label.setAttribute('class', 'btn btn-outline-primary');
        label.setAttribute('for', 'radio' + index);
        label.innerHTML = `<i class="bi-broadcast-pin"></i> ${item[0]}`;

        parent.insertBefore(radioInput, list_end);
        parent.insertBefore(label, list_end);
    });
    //label_x.remove();
    //button_x.remove();
}
//function radioList() {
//fetch('/get_radios')
//    .then((response) => response.json())
//    .then((data) => generateRadioList(data));
//}
//radioList();

function set_ui(data) {
    document.main.volume.value = data.volume;
    document.main.state.value = data.input;
};

function send_action(action) { fetch('/set?action=' + action.value) }
function send_volume(volume) { fetch('/set?volume=' + volume.value) }

// Server Sent Events (SSE)
const evtSource = new EventSource("/update");
evtSource.addEventListener("update_ui", (event) => { set_ui(JSON.parse(event.data)); });

// After loading page
document.addEventListener('DOMContentLoaded', () => {
    set_initial_theme();
    fetch('/get_radios')
        .then((response) => response.json())
        .then((data) => generateRadioList(data));
    more(document.getElementById('show_more'))
    fetch('/get')
        .then((response) => response.json())
        .then((data) => set_ui(data));
}, false);

