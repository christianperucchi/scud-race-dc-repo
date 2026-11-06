"""Paths used by the asset pipeline. Override with environment variables."""
import os
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SDK = os.environ.get('KATANA_SDK', os.path.join(ROOT, 'third_party', 'sdk'))            # folder that contains Ninja/
PACK = os.environ.get('TEXTURE_PACK', os.path.join(ROOT, 'third_party', 'texture_pack')) # folder that contains Demo/
SCUD_DIR = os.path.join(SDK, 'Ninja', 'Demo', 'scud') + '/'
DRIVE2_EXE = os.path.join(SDK, 'Ninja', 'Demo', 'drive2', 'demo.exe')
DRIVE2_TEX = os.path.join(PACK, 'Demo', 'drive2') + '/'
BUILD = os.path.join(ROOT, 'build')
ROMDISK = os.path.join(ROOT, 'romdisk')
os.makedirs(BUILD, exist_ok=True); os.makedirs(ROMDISK, exist_ok=True)
