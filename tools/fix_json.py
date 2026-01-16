"""
Fix truncated MGS2 animation JSON file
"""

import json
import re

JSON_FILE = r"D:\Metal Gear Solid 2\MGS2_Capture\animation_data.json"
OUTPUT_FILE = r"D:\Metal Gear Solid 2\MGS2_Capture\animation_data_fixed.json"

print("Reading truncated JSON file...")
with open(JSON_FILE, 'r') as f:
    content = f.read()

print(f"Original size: {len(content)} chars")

# Find the last complete frame by looking for the pattern "}," or "}" before truncation
# We need to find the last complete bone_matrices array

# Find last complete frame ending with proper structure
last_complete = content.rfind('        }\n      ]\n    }')
if last_complete == -1:
    last_complete = content.rfind('        }\r\n      ]\r\n    }')

if last_complete > 0:
    # Cut content to last complete frame
    content = content[:last_complete + len('        }\n      ]\n    }')]
    # Add closing brackets
    content += '\n  ]\n}\n'
    print(f"Fixed size: {len(content)} chars")
    
    # Verify it's valid JSON
    try:
        data = json.loads(content)
        print(f"SUCCESS! Fixed JSON has {len(data.get('frames', []))} frames")
        
        with open(OUTPUT_FILE, 'w') as f:
            f.write(content)
        print(f"Saved to: {OUTPUT_FILE}")
    except json.JSONDecodeError as e:
        print(f"Still invalid: {e}")
        # Try more aggressive fix - find last complete bone matrix
        last_bone = content.rfind('          ]\n        }')
        if last_bone > 0:
            content = content[:last_bone + len('          ]\n        }')]
            content += '\n      ]\n    }\n  ]\n}\n'
            with open(OUTPUT_FILE, 'w') as f:
                f.write(content)
            print(f"Saved aggressive fix to: {OUTPUT_FILE}")
else:
    print("Could not find truncation point")
