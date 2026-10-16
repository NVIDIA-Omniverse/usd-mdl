# SPDX-License-Identifier: Apache-2.0
"""Concrete builtin nodes through public Sdr/UsdShade, in isolated registries."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest


_FORMAT_ERROR = 'expected T[](...)<elementType,positiveCount>'
_COUNT_ERROR = 'invalid or unrepresentable element count'
_LOCAL_TYPE_ERROR = 'expected a local element type and canonical decimal count'
_SDK_ERROR = 'SDK rejected the specialization'
_REJECTED_REQUESTS = [
    ('mdl-builtin.mdl', 'T[]', _FORMAT_ERROR),
    ('mdl-builtin.mdl', 'T[](...)', _FORMAT_ERROR),
    ('mdl-builtin.mdl', 'T[](...)<float3,0>', 'zero elements require a typed empty constant'),
    ('mdl-builtin.mdl', 'T[](...)<float3,-1>', _COUNT_ERROR),
    ('mdl-builtin.mdl', 'T[](...)<float3,nope>', _COUNT_ERROR),
    ('mdl-builtin.mdl', 'T[](...)<float3,2>x', _FORMAT_ERROR),
    ('mdl-builtin.mdl', 'T[](...)<float3,02>', _LOCAL_TYPE_ERROR),
    ('mdl-builtin.mdl', 'T[](...)<float3,999999999999999999999>', _COUNT_ERROR),
    ('mdl-builtin.mdl', 'T[](...)<Missing,2>', _SDK_ERROR),
    ('mdl-builtin.mdl', 'T[](...)<float3,2,3>', _LOCAL_TYPE_ERROR),
    ('mdl-builtin.mdl', 'T[](...)<,2>', _FORMAT_ERROR),
    # Missing fails SDK type lookup before allocating ports, even at INT_MAX.
    ('mdl-builtin.mdl', 'T[](...)<Missing,2147483648>', _COUNT_ERROR),
    ('mdl-builtin.mdl', 'T[](...)<Missing,2147483647>', _SDK_ERROR),
    ('mdl-builtin.mdl', 'T[](...)<Light,2>', _SDK_ERROR),
    ('mdl-builtin.mdl', 'T[](...)<::array_nodes::Light,2>', _LOCAL_TYPE_ERROR),
    ('array_nodes.mdl', 'T[](...)<::array_nodes::Light,2>', _LOCAL_TYPE_ERROR),
    ('array_nodes.mdl', 'T[](...)<.::other_nodes::Light,2>', _LOCAL_TYPE_ERROR),
    ('array_nodes.mdl', 'T[](...)<..::array_nodes::Light,2>', _LOCAL_TYPE_ERROR),
    ('array_nodes.mdl', 'T[](...)<float,2>', _SDK_ERROR),
    ('array_nodes.mdl', 'T[](...)<color,2>', _SDK_ERROR),
    ('array_nodes.mdl', 'T[](...)<Missing,2>', _SDK_ERROR),
    ('import_only.mdl', 'T[](...)<Light,2>', _SDK_ERROR),
]


def worker(args):
    root = Path(os.environ['PXR_TEST_DIR'])
    for key in ('MDL_USER_PATH', 'MDL_SYSTEM_PATH', 'USD_MDL_ALLOW_FIRST_OVERLOAD'):
        os.environ.pop(key, None)
    os.environ['USD_MDL_SEARCH_PATHS'] = str(root/'mdl')
    os.environ['USD_MDL_ADD_USER_PATHS'] = '0'
    os.environ['USD_MDL_ADD_SYSTEM_PATHS'] = '0'
    os.environ['USD_MDL_DISCOVER_ON_STARTUP'] = '1' if args.mode == 'eager' else '0'
    from pxr import Sdf, Sdr, Tf, Usd, UsdShade, Vt
    import UsdMdl

    def lookup(sub, asset='mdl-builtin.mdl'):
        return Sdr.Registry().GetShaderNodeFromAsset(Sdf.AssetPath(asset), {}, sub, 'mdl')

    def ordinary(sub):
        return lookup(sub, 'array_nodes.mdl')

    def snapshot(node):
        return (node.GetIdentifier(), dict(node.GetMetadata()),
                [(str(n), str(node.GetShaderInput(n).GetType()),
                  dict(node.GetShaderInput(n).GetMetadata())) for n in node.GetShaderInputNames()])

    class Tests(unittest.TestCase):
        @classmethod
        def setUpClass(cls):
            if args.mode == 'registered':
                UsdMdl.AddModuleToRegistry(Sdf.AssetPath('array_nodes.mdl'))
                builtins = UsdMdl.AddModuleToRegistry(Sdf.AssetPath('mdl-builtin.mdl'))
                assert not any(n.GetMetadata().get('mdl:mdl_db_definition_name') == 'mdl::T[](...)' for n in builtins)
            elif args.mode == 'eager':
                definitions = {n.GetMetadata().get('mdl:mdl_db_definition_name')
                               for n in Sdr.Registry().GetShaderNodesByFamily()}
                expected = {f'mdl::array_nodes::overload({typ})' for typ in ('float', 'int')}
                assert expected <= definitions, expected - definitions
            # Both orders begin with a named type so cold module loading is real.
            cls.specs = [('array_nodes.mdl', 'Light', 2, 'token[]'),
                         ('mdl-builtin.mdl', 'float3', 1, 'float3[]'),
                         ('mdl-builtin.mdl', 'float3', 2, 'float3[]'),
                         ('mdl-builtin.mdl', 'float3', 3, 'float3[]'),
                         ('mdl-builtin.mdl', 'color', 2, 'color3f[]'),
                         ('mdl-builtin.mdl', 'float', 2, 'float[]'),
                         ('mdl-builtin.mdl', 'intensity_mode', 2, 'int[]'),
                         ('array_nodes.mdl', 'OtherLight', 2, 'token[]'),
                         ('array_nodes.mdl', 'Mode', 2, 'int[]'),
                         ('mdl-builtin.mdl', 'bool3', 2, 'bool[]'),
                         ('other_nodes.mdl', 'Light', 2, 'token[]'),
                         ('array_nodes.mdl', 'Packet', 2, 'token[]')]
            if args.order == 'reverse': cls.specs.reverse()
            cls.nodes = {}
            for asset, element, count, _ in cls.specs:
                sub = f'T[](...)<{element},{count}>'
                n = lookup(sub, asset)
                assert n and n.IsValid(), sub
                cls.nodes[asset, sub] = n

        def test_concrete_ports_and_identity(self):
            input_types = {
                'float3': ('float3', Sdf.ValueTypeNames.Float3, 0),
                'color': ('color3f', Sdf.ValueTypeNames.Color3f, 0),
                'float': ('float', Sdf.ValueTypeNames.Float, 0),
                'intensity_mode': ('int', Sdf.ValueTypeNames.Int, 0),
                'Mode': ('int', Sdf.ValueTypeNames.Int, 0),
                'bool3': ('bool[]', Sdf.ValueTypeNames.BoolArray, 3),
                'Light': ('struct', Sdf.ValueTypeNames.Token, 0),
                'OtherLight': ('struct', Sdf.ValueTypeNames.Token, 0),
                'Packet': ('struct', Sdf.ValueTypeNames.Token, 0),
            }
            identities = set()
            for asset, element, count, sdf in self.specs:
                sub = f'T[](...)<{element},{count}>'
                sdk_element = element if asset == 'mdl-builtin.mdl' else f'::{asset[:-4]}::{element}'
                with self.subTest(asset=asset, sub=sub):
                    n = self.nodes[asset, sub]; md = n.GetMetadata()
                    self.assertEqual(md['mdl:subIdentifier'], sub)
                    self.assertEqual(md['mdl:mdlNameWithSignature'], f'T[](...)<{sdk_element},{count}>')
                    self.assertEqual(md['mdl:mdl_db_definition_name'], 'mdl::T[](...)')
                    self.assertEqual(md['mdl:moduleUsdIdentifier'], asset)
                    if asset != 'mdl-builtin.mdl':
                        self.assertEqual(md['mdl:mdl_db_module_name'], f'mdl::{asset[:-4]}')
                        self.assertEqual(md['mdl:is_embedded_module'], 'false')
                        self.assertEqual(Path(n.GetResolvedImplementationURI()).resolve(), (root/'mdl'/asset).resolve())
                    self.assertEqual(list(n.GetShaderInputNames()), [f'value{i}' for i in range(count)])
                    self.assertEqual(list(n.GetShaderOutputNames()), ['out'])
                    out = n.GetShaderOutput('out'); om = out.GetMetadata()
                    self.assertTrue(out.GetTypeAsSdfType().HasSdfType())
                    self.assertEqual(str(out.GetTypeAsSdfType().GetSdfType()), sdf)
                    self.assertEqual(om['mdl.arrayIsImmediateSized'], 'true')
                    self.assertEqual(om['mdl.arraySize'], str(count))
                    self.assertNotIn('mdl.arrayDeferredSizeSymbol', om)
                    self.assertFalse(out.IsDynamicArray())
                    self.assertEqual(out.GetArraySize(), count * (3 if element == 'bool3' else 1))
                    self.assertIsNone(out.GetDefaultValue())
                    if sdf == 'token[]':
                        self.assertEqual(str(out.GetType()), 'struct')
                        self.assertEqual(om['mdl.symbol'], sdk_element)
                    for i in range(count):
                        prop = n.GetShaderInput(f'value{i}')
                        sdr_type, sdf_type, array_size = input_types[element]
                        self.assertEqual(prop.GetType(), sdr_type)
                        self.assertTrue(prop.GetTypeAsSdfType().HasSdfType())
                        self.assertEqual(prop.GetTypeAsSdfType().GetSdfType(), sdf_type)
                        self.assertEqual(prop.GetArraySize(), array_size)
                        self.assertFalse(prop.IsDynamicArray())
                        self.assertEqual(prop.GetMetadata()['mdl.hasDefault'], 'false')
                        self.assertIsNone(prop.GetDefaultValue())
                        self.assertIsNone(prop.GetDefaultValueAsSdfType())
                        self.assertNotIn('mdl.expressionKind', prop.GetMetadata())
                        if asset != 'mdl-builtin.mdl':
                            self.assertEqual(prop.GetMetadata()['mdl.symbol'], sdk_element)
                    self.assertEqual(lookup(sub, asset).GetIdentifier(), n.GetIdentifier())
                    identities.add(str(n.GetIdentifier()))
            self.assertEqual(len(identities), len(self.specs))

        def test_declarations_and_defaults(self):
            n = ordinary('shapes'); self.assertTrue(n and n.IsValid())
            for name, immediate, count in [('fixed',True,2), ('deferred',False,None),
                                            ('packed',True,2), ('empty_fixed',True,0),
                                            ('empty_default',False,None), ('nonempty_default',False,None)]:
                p = n.GetShaderInput(name); md = p.GetMetadata()
                self.assertEqual(md['mdl.arrayIsImmediateSized'], str(immediate).lower())
                self.assertEqual(md.get('mdl.arraySize'), str(count) if immediate else None)
                self.assertEqual('mdl.arrayDeferredSizeSymbol' in md, not immediate)
            for name in ('empty_fixed', 'empty_default'):
                p = n.GetShaderInput(name)
                self.assertIsInstance(p.GetDefaultValueAsSdfType(), Vt.TokenArray)
                self.assertEqual(len(p.GetDefaultValueAsSdfType()), 0)
                self.assertEqual(p.GetMetadata()['mdl.expressionKind'], 'Constant')
            self.assertIsNone(n.GetShaderInput('nonempty_default').GetDefaultValueAsSdfType())
            n = ordinary('defaults')
            for name, has, kind in [('required','false',None), ('called','true','Call')]:
                p = n.GetShaderInput(name)
                self.assertEqual(p.GetMetadata()['mdl.hasDefault'], has)
                self.assertEqual(p.GetMetadata().get('mdl.expressionKind'), kind)
                self.assertIsNone(p.GetDefaultValueAsSdfType())
            self.assertEqual(UsdMdl.Metadata.ArrayIsImmediateSized, 'mdl.arrayIsImmediateSized')
            self.assertEqual(UsdMdl.Metadata.ArraySize, 'mdl.arraySize')

        def test_helper_and_registration_stability(self):
            helper = ordinary('helper'); before = snapshot(helper)
            self.assertEqual(helper.GetMetadata()['mdl:subIdentifier'], 'helper')
            self.assertEqual(list(helper.GetShaderInputNames()), ['value0','value1'])
            for asset, sub in reversed(list(self.nodes)):
                self.assertEqual(lookup(sub, asset).GetIdentifier(), self.nodes[asset, sub].GetIdentifier())
            UsdMdl.AddModuleToRegistry(Sdf.AssetPath('array_nodes.mdl'))
            UsdMdl.AddModuleToRegistry(Sdf.AssetPath('mdl-builtin.mdl'))
            self.assertEqual(snapshot(ordinary('helper')), before)
            for (asset, sub), original in self.nodes.items():
                self.assertEqual(snapshot(lookup(sub, asset)), snapshot(original))
            for typ in ('float','int'):
                n = ordinary(f'overload({typ})')
                self.assertEqual(n.GetMetadata()['mdl:mdl_db_definition_name'], f'mdl::array_nodes::overload({typ})')

        def test_rejected_requests(self):
            imported = lookup('consume', 'import_only.mdl')
            self.assertTrue(imported and imported.IsValid())
            self.assertEqual(imported.GetShaderInput('value').GetMetadata()['mdl.symbol'],
                             '::array_nodes::Light')
            for asset, sub, _ in _REJECTED_REQUESTS:
                with self.subTest(asset=asset, sub=sub):
                    marker = f'{asset}:{sub}'
                    print('BEGIN_REJECTION ' + marker, file=sys.stderr, flush=True)
                    try:
                        n = lookup(sub, asset)
                    except Tf.ErrorException as error:
                        self.assertIn('returned null', str(error))
                        n = None
                    finally:
                        print('END_REJECTION ' + marker, file=sys.stderr, flush=True)
                    self.assertFalse(n and n.IsValid(), sub)

        def test_concurrent_requests(self):
            requests = [('T[](...)<float3,4>', 'mdl-builtin.mdl'),
                        ('T[](...)<Light,3>', 'array_nodes.mdl'),
                        ('T[](...)<Light,3>', 'other_nodes.mdl')] * 8
            with ThreadPoolExecutor(max_workers=4) as pool:
                nodes = list(pool.map(lambda request: lookup(*request), requests))
            identities = {}
            for (sub, asset), node in zip(requests, nodes):
                self.assertTrue(node and node.IsValid())
                count = 4 if 'float3' in sub else 3
                self.assertEqual(list(node.GetShaderInputNames()), [f'value{i}' for i in range(count)])
                self.assertEqual(node.GetMetadata()['mdl:subIdentifier'], sub)
                self.assertEqual(identities.setdefault((asset, sub), node.GetIdentifier()), node.GetIdentifier())
            self.assertEqual(len(set(identities.values())), 3)

        def test_documented_authoring_examples(self):
            guide = (Path(__file__).resolve().parents[1]/'docs/sub-identifiers.md').read_text(encoding='utf-8')
            section = guide.split('## Builtin array constructors\n',1)[1].split('## Overriding resource parameters',1)[0]
            mdl = re.search(r'```mdl\n(.*?)```',section,re.S).group(1)
            (root/'mdl/example.mdl').write_text(mdl,encoding='utf-8')
            examples = re.findall(r'```usda\n(.*?)```',section,re.S)
            self.assertEqual(len(examples),2)
            for text in examples:
                stage = Usd.Stage.CreateInMemory()
                self.assertTrue(stage.GetRootLayer().ImportFromString(text))
                for prim in stage.Traverse():
                    shader = UsdShade.Shader(prim)
                    node = shader.GetShaderNodeForSourceType('mdl')
                    self.assertTrue(node and node.IsValid())
                    for port in shader.GetInputs()+shader.GetOutputs():
                        output = str(port.GetFullName()).startswith('outputs:')
                        p = node.GetShaderOutput(port.GetBaseName()) if output else node.GetShaderInput(port.GetBaseName())
                        self.assertEqual(p.GetTypeAsSdfType().GetSdfType(),port.GetTypeName())
                        for key,value in port.GetSdrMetadata().items():
                            self.assertEqual(p.GetMetadata().get(key),value)

        def test_saved_mixed_graph(self):
            stage = Usd.Stage.CreateNew(str(root/'mixed.usda'))
            def shader(path, asset, sub):
                s = UsdShade.Shader.Define(stage,path)
                s.SetSourceAsset(Sdf.AssetPath(asset),'mdl'); s.SetSourceAssetSubIdentifier(sub,'mdl')
                return s
            light = shader('/Light','array_nodes.mdl','Light(float,color)')
            light.CreateOutput('out',Sdf.ValueTypeNames.Token)
            helper = shader('/Helper','array_nodes.mdl','helper')
            array = shader('/Array','array_nodes.mdl','T[](...)<Light,2>')
            for s in (helper,array):
                for name in ('value0','value1'):
                    s.CreateInput(name,Sdf.ValueTypeNames.Token).ConnectToSource(light.GetOutput('out'))
                s.CreateOutput('out',Sdf.ValueTypeNames.TokenArray)
            # Deliberately stale USD metadata must not replace SDK-derived Sdr.
            array.GetOutput('out').SetSdrMetadata({'mdl.arraySize':'99'})
            stage.GetRootLayer().Save()
            run = subprocess.run([sys.executable,__file__,'--reload'],capture_output=True,text=True,timeout=60)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

        def test_offpath_relocation(self):
            # Exercise the existing experimental off-search-path loader through
            # ordinary UsdShade lookup, without a custom Sdr request adapter.
            with tempfile.TemporaryDirectory(prefix='array owner ') as temp:
                original = Path(temp)/'original'
                original.mkdir()
                (original/'types.mdl').write_text(
                    'mdl 1.8; export struct Light { float intensity; };\n', encoding='utf-8')
                stage = Usd.Stage.CreateNew(str(original/'scene.usda'))
                shader = UsdShade.Shader.Define(stage, '/Array')
                shader.SetSourceAsset(Sdf.AssetPath('./types.mdl'), 'mdl')
                shader.SetSourceAssetSubIdentifier('T[](...)<Light,2>', 'mdl')
                shader.CreateOutput('out', Sdf.ValueTypeNames.TokenArray)
                stage.GetRootLayer().Save()
                relocated = Path(temp)/'relocated'
                shutil.copytree(original, relocated)
                symbols = []
                for directory in (original, relocated):
                    run = subprocess.run([sys.executable, __file__, '--offpath-stage',
                                          str(directory/'scene.usda')],
                                         capture_output=True, text=True, timeout=60)
                    self.assertEqual(run.returncode, 0, run.stdout+run.stderr)
                    symbols.append(re.search(r'OWNER_SYMBOL=(.*)', run.stdout).group(1))
                self.assertNotEqual(*symbols)
                self.assertEqual((original/'scene.usda').read_bytes(), (relocated/'scene.usda').read_bytes())

    try:
        if args.offpath_stage:
            path = Path(args.offpath_stage)
            stage = Usd.Stage.Open(str(path))
            shader = UsdShade.Shader(stage.GetPrimAtPath('/Array'))
            node = shader.GetShaderNodeForSourceType('mdl')
            assert node and node.IsValid()
            md = node.GetMetadata()
            assert md['mdl:moduleUsdIdentifier'] == ''
            assert md['mdl:subIdentifier'] == 'T[](...)<Light,2>'
            assert md['mdl:mdl_db_definition_name'] == 'mdl::T[](...)'
            assert Path(node.GetResolvedImplementationURI()).resolve() == (path.parent/'types.mdl').resolve()
            symbol = node.GetShaderOutput('out').GetMetadata()['mdl.symbol']
            assert md['mdl:mdlNameWithSignature'] == f'T[](...)<{symbol},2>'
            assert md['mdl:mdl_db_module_name'] == 'mdl' + symbol.rsplit('::', 1)[0]
            assert symbol.endswith('::types::Light')
            assert symbol not in path.read_text(encoding='utf-8')
            print('OWNER_SYMBOL=' + symbol)
            return 0
        if args.reload:
            stage = Usd.Stage.Open(str(root/'mixed.usda'))
            # Specialization first in an entirely new process; ordinary nodes follow.
            for path in ('/Array','/Helper','/Light'):
                shader = UsdShade.Shader(stage.GetPrimAtPath(path))
                node = shader.GetShaderNodeForSourceType('mdl')
                assert node and node.IsValid(), path
                output_type = Sdf.ValueTypeNames.Token if path == '/Light' else Sdf.ValueTypeNames.TokenArray
                assert shader.GetOutput('out').GetTypeName() == output_type, path
                assert node.GetShaderOutput('out').GetTypeAsSdfType().GetSdfType() == output_type, path
                if path != '/Light':
                    for name in ('value0', 'value1'):
                        port = shader.GetInput(name)
                        assert port.GetTypeName() == Sdf.ValueTypeNames.Token, (path, name)
                        assert port.GetAttr().GetConnections() == [Sdf.Path('/Light.outputs:out')], (path, name)
                        assert node.GetShaderInput(name).GetTypeAsSdfType().GetSdfType() == Sdf.ValueTypeNames.Token
                if path == '/Array': assert node.GetShaderOutput('out').GetMetadata()['mdl.arraySize'] == '2'
            return 0
        result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Tests))
        return 0 if result.testsRun > 0 and result.wasSuccessful() else 1
    finally:
        UsdMdl.Shutdown()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--mode',choices=('cold','registered','eager'),default='cold')
    parser.add_argument('--order',choices=('forward','reverse'),default='forward')
    parser.add_argument('--reload',action='store_true')
    parser.add_argument('--offpath-stage')
    parser.add_argument('--worker',action='store_true')
    args = parser.parse_args()
    if args.worker or args.reload or args.offpath_stage:
        raise SystemExit(worker(args))
    run = subprocess.run([sys.executable,__file__,*sys.argv[1:],'--worker'],
                         capture_output=True,text=True,timeout=120)
    output = run.stdout + run.stderr
    print(output,end='',flush=True)
    if run.returncode:
        raise SystemExit(run.returncode)
    # Attribute each native warning to its request, not another rejected row.
    for asset, sub, reason in _REJECTED_REQUESTS:
        marker = f'{asset}:{sub}'
        begin, end = 'BEGIN_REJECTION ' + marker + '\n', 'END_REJECTION ' + marker + '\n'
        assert run.stderr.count(begin) == run.stderr.count(end) == 1, marker
        diagnostic = run.stderr.split(begin, 1)[1].split(end, 1)[0]
        assert f"MDL array constructor '{sub}': {reason}" in diagnostic, (marker, diagnostic)
