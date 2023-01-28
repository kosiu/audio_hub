#!/usr/bin/env python3
import time
import threading
import json
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

def run(state_in):
    global state
    state = state_in
    server = threading.Thread(target=thread)
    server.start()

if __name__ == "__main__":
    pass
    # for stand alone testing require mockup of the: "state" class
    # state.set_action(str)
    # state.set_volume(int)
    # state.get_ui_state()
    # state.update_ui

