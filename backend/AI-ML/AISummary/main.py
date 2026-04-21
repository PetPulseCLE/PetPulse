from fastapi import FastAPI

from api.routes import router


app = FastAPI(title="Pet Health AI API", version="0.1.0")
app.include_router(router)


@app.get("/health")
async def health_check():
    return {"status": "ok"}
