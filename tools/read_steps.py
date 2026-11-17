import json, sys
sys.stdout.reconfigure(encoding='utf-8')

with open('C:/Users/sockm/.gemini/antigravity/brain/86a4e033-acb4-4ed9-a0a3-9cb07aeed087/.system_generated/logs/transcript.jsonl', 'r', encoding='utf-8', errors='ignore') as f:
    for line in f:
        d = json.loads(line)
        idx = d.get('step_index')
        if idx in range(7280, 7320):
            print(f"=== STEP {idx} ({d.get('type')}) ===")
            if d.get('tool_calls'):
                print("Tool calls:", json.dumps(d.get('tool_calls'), ensure_ascii=False)[:300])
            if d.get('content'):
                print("Content:", d.get('content')[:300])
            if d.get('thinking'):
                print("Thinking:", d.get('thinking')[:300])
