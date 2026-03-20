import asyncio
import aiocoap
import aiocoap.resource as resource
import json
from datetime import datetime

class CanteenResource(resource.Resource):
    async def render_post(self, request):
        try:
            payload = request.payload.decode()
            data    = json.loads(payload)

            # Print to terminal
            #print(f"[{datetime.fromtimestamp(data['time']).strftime('%Y-%m-%d %H:%M:%S')}] {data}")

            readable = datetime.fromtimestamp(data['time']).strftime('%Y-%m-%d %H:%M:%S')
            data_to_print = data.copy()
            data_to_print.pop('time', None)  # remove 'time' field

            print(f"[{readable}] {data_to_print}")

            # Save to file
            with open("canteen_log.json", "a") as f:
                f.write(json.dumps(data) + "\n")

        except Exception as e:
            print("Failed to parse payload:", e)

        return aiocoap.Message(code=aiocoap.CHANGED)

async def main():
    root = resource.Site()
    root.add_resource(['canteen'], CanteenResource())
    await aiocoap.Context.create_server_context(root, bind=('10.44.35.129', 5683))
    print("CoAP server running on port 5683...")
    await asyncio.get_event_loop().create_future()

asyncio.run(main())