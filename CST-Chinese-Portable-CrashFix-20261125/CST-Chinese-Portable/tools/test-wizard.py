"""Offline DOM regression; only a generated fixture is opened in headless Edge."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import uuid

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / 'src/CSTRibbonZh/web'

parser = argparse.ArgumentParser()
parser.add_argument('--browser', default=str(Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Microsoft/Edge/Application/msedge.exe'))
parser.add_argument('--cst-root', type=Path)
args = parser.parse_args()

def pairs(items):
    result = {}
    for a, b in items:
        assert a not in result, 'duplicate dictionary key: ' + a
        result[a] = b
    return result

dictionary = json.loads((WEB / 'wizard-zh.json').read_text('utf-8'), object_pairs_hook=pairs)
norm = lambda s: ' '.join(s.split()).casefold()
assert len({norm(k) for k in dictionary}) == len(dictionary), 'normalized duplicate keys'
assert all(re.search('[\u3400-\u9fff]', v) for v in dictionary.values()), 'untranslated dictionary value'
print(f'PASS {len(dictionary)} wizard dictionary entries')

if args.cst_root:
    titles = set()
    folder = args.cst_root / 'Library/ConfigurationWizard/js/cst'
    for path in folder.glob('*_workflows.js'):
        # CST ships these legacy workflow files in Windows-1252 (including en dashes).
        raw = path.read_bytes()
        try: source = raw.decode('utf-8')
        except UnicodeDecodeError: source = raw.decode('cp1252')
        titles.update(re.findall(r'title\s*:\s*"([^"\r\n]*)"', source))
    missing = sorted(t for t in titles if norm(t) not in {norm(k) for k in dictionary})
    assert titles, 'No workflow source found'
    assert not missing, 'Missing workflow titles: ' + repr(missing)
    print(f'PASS {len(titles)} static workflow titles covered (not a whole-app coverage metric)')

work = Path(tempfile.gettempdir()) / ('cstzh-web-' + uuid.uuid4().hex[:8])
work.mkdir(parents=True)
fixture = work / 'fixture.html'
template = (ROOT / 'tests/wizard-fixture.html').read_text('utf-8')
script = (WEB / 'wizard.js').read_text('utf-8')
test = r'''
const checks=[];
function assert(ok,name){if(!ok)throw new Error(name);checks.push(name);}
const apply = SCRIPT;
const dictionary = DICTIONARY;
try {
    apply(dictionary,'file:///unrelated/index.html');
    assert(document.querySelector('#myHeader').textContent==='Create Project Template','foreign URL refused');
    const input=document.querySelector('#minFreq');
    input.focus(); input.setSelectionRange(0,2);
    let clicks=0;
    document.querySelector('#nextButton').addEventListener('click',()=>clicks++);
    apply(dictionary,location.href);
    assert(document.querySelector('#myHeader').textContent==='创建工程模板','heading translated');
    assert(document.querySelector('#cancelButton').textContent==='取消','existing dictionary button translated');
    assert(document.querySelector('#applicationOptionsListView h3').textContent==='天线','workflow display translated');
    assert(document.querySelector('#appLink').getAttribute('href')==='#original','link target intact');
    assert(document.querySelector('[data-id]').getAttribute('data-id')==='antenna','selection ID intact');
    assert(input.value==='2.45' && document.activeElement===input && input.selectionEnd===2,'input value and focus intact');
    assert(document.querySelector('#settingsMaxUnit').textContent==='GHz','units intact');
    assert(document.querySelector('#configNameTextBox').value==='Antennas','template name intact');
    assert(document.querySelector('#userCell').textContent==='Antennas','summary value intact');
    assert(document.querySelector('#userEditable').textContent==='Antennas','editable content intact');
    assert(document.querySelector('#originalScript').textContent==='Antennas','scripts intact');
    document.querySelector('#nextButton').click();
    assert(clicks===1,'button handler intact');
    apply(dictionary,location.href);
    assert(document.querySelectorAll('#nextButton').length===1,'repeat injection idempotent');
    document.querySelector('#workflowOptionsListView').innerHTML='<h3>Planar (Patch, Slot, etc.)</h3>';
    document.querySelector('#nextButton').firstChild.nodeValue='Next >';
    setTimeout(()=>{
        try{
            assert(document.querySelector('#workflowOptionsListView h3').textContent==='平面天线（贴片、缝隙等）','late nested workflow translated');
            assert(document.querySelector('#nextButton').textContent==='下一步 >','reset button retranslated');
            document.querySelector('#testResult').textContent='PASS '+checks.length+' DOM checks';
            document.querySelector('#testResult').setAttribute('data-result','PASS');
        }catch(e){document.querySelector('#testResult').textContent='FAIL '+e.message;}
    },250);
}catch(e){document.querySelector('#testResult').textContent='FAIL '+e.message;}
'''.replace('SCRIPT', '('+script+')').replace('DICTIONARY', json.dumps(dictionary, ensure_ascii=False))
fixture.write_text(template+'<script>'+test+'</script>', encoding='utf-8')
command = [args.browser,'--headless','--disable-gpu','--no-first-run','--no-default-browser-check',
           '--user-data-dir='+str(work/'profile'),'--dump-dom','--virtual-time-budget=2500',fixture.as_uri()]
run = subprocess.run(command, capture_output=True, timeout=45)
output = run.stdout.decode('utf-8', errors='replace')
(work/'browser-output.txt').write_text(output, encoding='utf-8')
(work/'browser-error.txt').write_bytes(run.stderr)
assert run.returncode == 0 and 'data-result="PASS"' in output, f'Headless test failed; inspect {work}'
match = re.search(r'<pre[^>]*id="testResult"[^>]*>(.*?)</pre>', output, re.S)
print(match.group(1) if match else 'PASS DOM regression')
