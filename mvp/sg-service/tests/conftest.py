import sys
from pathlib import Path

# Make the sg-service package importable from tests/
sys.path.insert(0, str(Path(__file__).parent.parent))
