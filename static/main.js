function send_action(action) {fetch('/set?action='+action.value)}
function send_volume(volume) {fetch('/set?volume='+volume.value)}

function set_initial_theme() {
    var theme = localStorage.getItem('theme');
    if (theme == null) {
        theme = window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
    }
    document.documentElement.setAttribute('data-bs-theme', theme)
    document.getElementById('theme').checked = (theme == 'dark')? true : false;
};
set_initial_theme();

function set_ui(data){
    document.main.volume.value = data.volume;
    document.main.state.value  = data.input;   
};

function more(btn){
    if (btn.checked){btn.labels[0].innerHTML = "Pokaż mniej";}
    else            {btn.labels[0].innerHTML = "Pokaż więcej";}
    const hidden_element = document.querySelector('.collapse')
    if ((hidden_element.clientHeight > 0 && !btn.checked) ||
        (hidden_element.clientHeight == 0 && btn.checked)){
            new bootstrap.Collapse('#hidden_part')
    }
      
}

function dark_theme(btn){
    var theme = (btn.checked == true) ? 'dark' : 'light';
    document.documentElement.setAttribute('data-bs-theme', theme)
    localStorage.setItem('theme',theme);
};

const evtSource = new EventSource("/update");
evtSource.addEventListener("update_ui", (event) => {set_ui(JSON.parse(event.data));});

document.addEventListener('DOMContentLoaded', () => {
    more(document.getElementById('show_more'))

    fetch('/get')
    .then((response) => response.json())
    .then((data) => set_ui(data));
}, false);
