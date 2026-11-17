import json
for line in open(r'C:/Users/sockm/.gemini/antigravity/brain/86a4e033-acb4-4ed9-a0a3-9cb07aeed087/.system_generated/logs/transcript.jsonl', encoding='utf-8'):
  obj = json.loads(line)
  if obj.get('step_index') == 6604:
    print(obj['tool_calls'])
