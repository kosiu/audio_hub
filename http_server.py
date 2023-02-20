#!/usr/bin/env -S python3 -u
import asyncio, time, threading, json
import uvicorn
import fastapi
from fastapi.staticfiles import StaticFiles
from sse_starlette.sse import EventSourceResponse

app = fastapi.FastAPI()
app.mount('/static',StaticFiles(directory="static"))

@app.get("/")
async def Home_page():
    return fastapi.responses.RedirectResponse('/static/index.html')
	
@app.get("/get")
async def Get():
    return state.get_ui_state()

@app.get("/set")
async def Set(action=None,volume=None):
    if action!=None: state.set_action(action)
    if volume!=None: state.set_volume(int(volume))
    return state.get_ui_state()

@app.get('/update')
async def State_update(request: fastapi.Request):
    response = dict(event='update_ui', retry=15000)
    async def event_generator():
        while True:
            if await request.is_disconnected(): break
            await state.update_ui.wait()
            state.update_ui.clear()
            response["data"] = json.dumps(state.get_ui_state())           
            yield response
    return EventSourceResponse(event_generator())

def thread():
    time.sleep(10)
    uvicorn.run("http_server:app", host="192.168.1.18", port=8000, log_level="warning")

def run_thread(state_in):
    global state
    state = state_in
    server = threading.Thread(target=thread)
    server.start()

class State:
    '''Mockup prepared for testing Web UI in production is overvriten by corct one'''
    def __init__(self):
        self.update_ui = asyncio.Event()
        self.state = dict(input='off',volume=10)
        self.dac_inputs = ['bt','pc','tv','off']
        self.do_nothing = ['stereo','reboot','pair']
        self.update_ui.set()

    def set_action(self, action):
        print(f'Action: {action}')
        if type(action) == int or action.isdigit(): self.state['input'] = int(action)
        elif action in self.dac_inputs: self.state['input'] = action
        elif action in self.do_nothing: pass
        else: print(f'Unknown action: {action}')
        self.update_ui.set()

    def set_volume(self, volume):
        self.state['volume']=volume
        self.update_ui.set()

    def get_ui_state(self): return self.state
state = State()

if __name__ == "__main__":
    # to run test: uvicorn http_server:app --reload-include '*, static/*'
    pass

