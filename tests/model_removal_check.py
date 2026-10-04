"""Linux worker integration check: python3 tests/model_removal_check.py /path/to/rawlab-preview."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    env = dict(os.environ, XDG_CONFIG_HOME=str(root / 'config'), XDG_DATA_HOME=str(root / 'data'))

    def request(**payload):
        result = subprocess.run([sys.argv[1], '--serve'], input=json.dumps(payload) + '\n',
                                text=True, capture_output=True, env=env, timeout=20, check=True)
        return json.loads(result.stdout)

    local = root / 'personal.onnx'
    local.write_bytes(b'local model')
    assert request(operation='model_add', task='denoise', path=str(local), name='Personal',
                   adapter='rawnind-bayer-v1').get('saved')
    model = next(m for m in request(operation='settings')['denoise'] if m['name'] == 'Personal')
    assert request(operation='model_remove', task='denoise', id=model['id']).get('saved')
    assert local.read_bytes() == b'local model'

    managed = root / 'data/RawLab/RawLab/models/removal-test/1.0'
    managed.mkdir(parents=True)
    (managed / 'model.onnx').write_bytes(b'downloaded model')
    config = root / 'config/RawLab/RawLab.conf'
    config.write_text('[denoiseModels]\nsize=1\n1\\id=hf:removal-test\n1\\name=Removal test\n'
                      f'1\\path={managed}/model.onnx\n1\\sourceId=removal-test\n'
                      '1\\version=1.0\n1\\origin=verified\n1\\adapter=rawnind-bayer-v1\n')
    assert request(operation='model_remove', task='denoise', id='hf:removal-test').get('saved')
    assert not managed.exists()
    assert not request(operation='settings')['denoise']
    print('Managed model removed; local model preserved')
