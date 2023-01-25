#!/usr/bin/env python3
import asyncio
import json
import uvicorn
import fastapi
from fastapi.staticfiles import StaticFiles
from sse_starlette.sse import EventSourceResponse

class State:
    def __init__(self):
        self.input = 'off'
        self.volume = 5 # to be replaced
        self.update_ui = asyncio.Event()

    def set_action(self, action):
        self.input = action
        self.update_ui.set()

    def get_input(self):
        return self.input

    def set_volume(self, volume):
        self.volume=volume # to be replaced
        self.update_ui.set()

    def get_volume(self):
        return self.volume # to be replaced

    def get_ui_state(self):
        return dict(input=self.input,volume=self.volume) # volume to be replaced

state = State()

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

def run():
    uvicorn.run("http_server:app", host="192.168.1.18", port=8000, log_level="info")

if __name__ == "__main__":
    run()

