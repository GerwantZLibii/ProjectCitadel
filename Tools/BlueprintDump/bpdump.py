"""Read-only dump of Blueprint assets (.uasset) as text, without the Unreal editor.

Supports uncooked UE 5.3 editor assets (the format used in this repo). It prints:
  * exported objects (components, CDOs, SCS nodes) with their non-default properties,
  * compiled Blueprint functions decompiled from Kismet bytecode into pseudo-code,
    with event entry points of the event graph (ubergraph) labelled.

Usage (assets are Git LFS objects - run `git lfs pull` first):
  python3 Tools/BlueprintDump/bpdump.py code    Content/ProjectCitadel/Core/Unit/BP_Worker.uasset
  python3 Tools/BlueprintDump/bpdump.py props   Content/ProjectCitadel/Core/Unit/BP_Worker.uasset
  python3 Tools/BlueprintDump/bpdump.py exports <asset> | names <asset> | all <asset>
  python3 Tools/BlueprintDump/bpdump.py tree    Content/ProjectCitadel <out_dir>

`tree` writes <Folder>__<Asset>.code.txt and .props.txt for every asset under a folder,
which makes Blueprint changes reviewable with a normal text diff.
"""
import os
import struct
import sys


class R:
    def __init__(self, data, pos=0):
        self.d = data
        self.p = pos

    def u8(self):
        v = self.d[self.p]; self.p += 1; return v

    def u16(self):
        v = struct.unpack_from('<H', self.d, self.p)[0]; self.p += 2; return v

    def i32(self):
        v = struct.unpack_from('<i', self.d, self.p)[0]; self.p += 4; return v

    def u32(self):
        v = struct.unpack_from('<I', self.d, self.p)[0]; self.p += 4; return v

    def i64(self):
        v = struct.unpack_from('<q', self.d, self.p)[0]; self.p += 8; return v

    def u64(self):
        v = struct.unpack_from('<Q', self.d, self.p)[0]; self.p += 8; return v

    def f32(self):
        v = struct.unpack_from('<f', self.d, self.p)[0]; self.p += 4; return v

    def f64(self):
        v = struct.unpack_from('<d', self.d, self.p)[0]; self.p += 8; return v

    def raw(self, n):
        v = self.d[self.p:self.p + n]; self.p += n; return v

    def fstring(self):
        n = self.i32()
        if n == 0:
            return ''
        if n > 0:
            s = self.raw(n)[:-1].decode('latin-1')
        else:
            s = self.raw(-n * 2)[:-2].decode('utf-16-le', 'replace')
        return s


class Asset:
    def __init__(self, path):
        self.path = path
        self.d = open(path, 'rb').read()
        r = R(self.d)
        tag = r.u32()
        assert tag == 0x9E2A83C1, hex(tag)
        legacy = r.i32()
        if legacy != -4:
            r.i32()
        self.ue4 = r.i32()
        self.ue5 = r.i32() if legacy <= -8 else 0
        r.i32()
        n = r.i32()
        self.custom = {}
        for _ in range(n):
            g = r.raw(16)
            self.custom[g.hex()] = r.i32()
        self.total_header = r.i32()
        self.package_name = r.fstring()
        self.pkg_flags = r.u32()
        name_count = r.i32(); name_off = r.i32()
        if self.ue5 >= 1008:
            r.i32(); r.i32()
        if not (self.pkg_flags & 0x80000000):
            r.fstring()
        r.i32(); r.i32()
        export_count = r.i32(); export_off = r.i32()
        import_count = r.i32(); import_off = r.i32()
        # names
        r2 = R(self.d, name_off)
        self.names = []
        for _ in range(name_count):
            self.names.append(r2.fstring())
            r2.u16(); r2.u16()
        # imports
        r2 = R(self.d, import_off)
        self.imports = []
        for _ in range(import_count):
            cpkg = self.fname(r2); cname = self.fname(r2); outer = r2.i32(); oname = self.fname(r2)
            if self.ue4 >= 520:
                self.fname(r2)
            if self.ue5 >= 1003:
                r2.i32()
            self.imports.append(dict(class_package=cpkg, class_name=cname, outer=outer, name=oname))
        # exports
        r2 = R(self.d, export_off)
        self.exports = []
        for _ in range(export_count):
            e = {}
            e['class'] = r2.i32(); e['super'] = r2.i32(); e['template'] = r2.i32(); e['outer'] = r2.i32()
            e['name'] = self.fname(r2); e['flags'] = r2.u32()
            e['size'] = r2.i64(); e['offset'] = r2.i64()
            r2.i32(); r2.i32(); r2.i32()
            if self.ue5 < 1005:
                r2.raw(16)
            if self.ue5 >= 1006:
                r2.i32()
            r2.u32(); r2.i32(); r2.i32()
            if self.ue5 >= 1003:
                r2.i32()
            for _ in range(5):
                r2.i32()
            self.exports.append(e)

    def fname(self, r):
        i = r.i32(); n = r.i32()
        if i < 0 or i >= len(self.names):
            return f'<badname {i}>'
        s = self.names[i]
        return s if n == 0 else f'{s}_{n - 1}'

    def obj_name(self, idx, full=False):
        if idx == 0:
            return 'None'
        if idx < 0:
            im = self.imports[-idx - 1]
            if full and im['outer']:
                return self.obj_name(im['outer'], True) + '.' + im['name']
            return im['name']
        if idx - 1 >= len(self.exports):
            return f'<badexp {idx}>'
        ex = self.exports[idx - 1]
        if full and ex['outer']:
            return self.obj_name(ex['outer'], True) + '.' + ex['name']
        return ex['name']

    def class_name(self, e):
        return self.obj_name(e['class'])

    # ---------------- tagged properties ----------------
    def read_tag(self, r):
        name = self.fname(r)
        if name == 'None':
            return None
        t = dict(name=name, type=self.fname(r))
        t['size'] = r.i32(); t['idx'] = r.i32()
        ty = t['type']
        if ty == 'StructProperty':
            t['struct'] = self.fname(r); r.raw(16)
        elif ty == 'BoolProperty':
            t['bool'] = r.u8()
        elif ty in ('ByteProperty', 'EnumProperty'):
            t['enum'] = self.fname(r)
        elif ty in ('ArrayProperty', 'SetProperty', 'OptionalProperty'):
            t['inner'] = self.fname(r)
        elif ty == 'MapProperty':
            t['inner'] = self.fname(r); t['value'] = self.fname(r)
        if r.u8():
            r.raw(16)
        return t

    def read_props(self, r, end, depth=0):
        out = []
        while r.p < end:
            t = self.read_tag(r)
            if t is None:
                break
            start = r.p
            if t['size'] < 0 or start + t['size'] > end:
                break
            try:
                v = self.read_value(r, t, start + t['size'], depth)
            except Exception as ex:  # noqa
                v = f'<err {ex}>'
            r.p = start + t['size']
            nm = t['name'] + (f'[{t["idx"]}]' if t['idx'] else '')
            out.append((nm, v))
        return out

    SIMPLE_STRUCTS = {
        'Vector': ('<ddd', 24), 'Rotator': ('<ddd', 24), 'Vector2D': ('<dd', 16),
        'LinearColor': ('<ffff', 16), 'Quat': ('<dddd', 32), 'Vector4': ('<dddd', 32),
        'IntPoint': ('<ii', 8), 'IntVector': ('<iii', 12),
    }

    def read_simple(self, r, ty, enum=None):
        if ty == 'IntProperty':
            return r.i32()
        if ty == 'UInt32Property':
            return r.u32()
        if ty == 'UInt64Property':
            return r.u64()
        if ty == 'UInt16Property':
            return r.u16()
        if ty == 'Int64Property':
            return r.i64()
        if ty == 'FloatProperty':
            return round(r.f32(), 4)
        if ty == 'DoubleProperty':
            return round(r.f64(), 4)
        if ty in ('NameProperty', 'EnumProperty'):
            return self.fname(r)
        if ty == 'StrProperty':
            return r.fstring()
        if ty in ('ObjectProperty', 'ClassProperty', 'InterfaceProperty', 'WeakObjectProperty', 'LazyObjectProperty'):
            return '@' + self.obj_name(r.i32(), True)
        if ty == 'ByteProperty':
            if enum and enum != 'None':
                return self.fname(r)
            return r.u8()
        if ty == 'BoolProperty':
            return bool(r.u8())
        if ty in ('SoftObjectProperty', 'SoftClassProperty'):
            p = self.fname(r); a = self.fname(r); s = r.fstring()
            return f'soft:{p}.{a}{":" + s if s else ""}'
        raise ValueError('nosimple ' + ty)

    def read_value(self, r, t, end, depth):
        ty = t['type']
        if ty == 'BoolProperty':
            return bool(t['bool'])
        if ty == 'StructProperty':
            return self.read_struct(r, t['struct'], end, depth)
        if ty == 'ArrayProperty':
            n = r.i32()
            inner = t['inner']
            if n < 0 or n > 20000:
                raise ValueError(f'bad array count {n}')
            if inner == 'StructProperty':
                it = self.read_tag(r)
                return [self.read_struct(r, it['struct'], end, depth + 1) for _ in range(n)]
            return [self.read_simple(r, inner) for _ in range(n)]
        if ty == 'TextProperty':
            flags = r.u32(); ht = r.u8()
            if ht == 0:
                ns = r.fstring(); key = r.fstring(); src = r.fstring()
                return f'text:{src!r}'
            if ht == 255:
                if r.i32():
                    return f'text:{r.fstring()!r}'
                return 'text:'
            return f'text<{ht}>'
        if ty == 'MapProperty':
            r.i32(); n = r.i32()
            if n < 0 or n > 20000:
                raise ValueError(f'bad map count {n}')
            items = []
            for _ in range(n):
                try:
                    k = self.read_simple(r, t['inner'])
                    if t['value'] == 'StructProperty':
                        v = self.read_props(r, end, depth + 1)
                    elif t['value'] == 'TextProperty':
                        v = self.read_value(r, dict(type='TextProperty'), end, depth)
                    else:
                        v = self.read_simple(r, t['value'])
                except Exception:
                    items.append('...'); break
                items.append((k, v))
            return dict(items) if all(isinstance(i, tuple) for i in items) else items
        if ty == 'MulticastInlineDelegateProperty' or ty == 'MulticastSparseDelegateProperty':
            n = r.i32()
            if n < 0 or n > 1000:
                raise ValueError('bad delegate count')
            return [f'{self.obj_name(r.i32())}.{self.fname(r)}' for _ in range(n)]
        if ty == 'DelegateProperty':
            return f'{self.obj_name(r.i32())}.{self.fname(r)}'
        return self.read_simple(r, ty, t.get('enum'))

    def read_struct(self, r, sname, end, depth):
        if sname in self.SIMPLE_STRUCTS:
            fmt, sz = self.SIMPLE_STRUCTS[sname]
            vals = struct.unpack_from(fmt, self.d, r.p)
            r.p += sz
            return tuple(round(x, 3) for x in vals)
        if sname == 'Guid':
            return r.raw(16).hex()
        if sname == 'Color':
            return tuple(r.raw(4))
        if sname in ('GameplayTagContainer',):
            n = r.i32(); return [self.fname(r) for _ in range(n)]
        if sname == 'SoftObjectPath' or sname == 'SoftClassPath':
            p = self.fname(r); a = self.fname(r); s = r.fstring()
            return f'soft:{p}.{a}'
        if sname in ('Timespan', 'DateTime'):
            return r.i64()
        if sname == 'FrameNumber':
            return r.i32()
        return dict(self.read_props(r, end, depth + 1))

    def export_props(self, e):
        r = R(self.d, e['offset'])
        end = e['offset'] + e['size']
        return self.read_props(r, end)


# ---------------- Kismet ----------------
EX = {
    0x00: 'LocalVariable', 0x01: 'InstanceVariable', 0x02: 'DefaultVariable', 0x04: 'Return', 0x06: 'Jump',
    0x07: 'JumpIfNot', 0x09: 'Assert', 0x0B: 'Nothing', 0x0C: 'NothingInt32', 0x0F: 'Let', 0x11: 'BitFieldConst',
    0x12: 'ClassContext', 0x13: 'MetaCast', 0x14: 'LetBool', 0x15: 'EndParmValue', 0x16: 'EndFunctionParms',
    0x17: 'Self', 0x18: 'Skip', 0x19: 'Context', 0x1A: 'Context_FailSilent', 0x1B: 'VirtualFunction',
    0x1C: 'FinalFunction', 0x1D: 'IntConst', 0x1E: 'FloatConst', 0x1F: 'StringConst', 0x20: 'ObjectConst',
    0x21: 'NameConst', 0x22: 'RotationConst', 0x23: 'VectorConst', 0x24: 'ByteConst', 0x25: 'IntZero',
    0x26: 'IntOne', 0x27: 'True', 0x28: 'False', 0x29: 'TextConst', 0x2A: 'NoObject', 0x2B: 'TransformConst',
    0x2C: 'IntConstByte', 0x2D: 'NoInterface', 0x2E: 'DynamicCast', 0x2F: 'StructConst', 0x30: 'EndStructConst',
    0x31: 'SetArray', 0x32: 'EndArray', 0x33: 'PropertyConst', 0x34: 'UnicodeStringConst', 0x35: 'Int64Const',
    0x36: 'UInt64Const', 0x37: 'DoubleConst', 0x38: 'Cast', 0x39: 'SetSet', 0x3A: 'EndSet', 0x3B: 'SetMap',
    0x3C: 'EndMap', 0x3D: 'SetConst', 0x3E: 'EndSetConst', 0x3F: 'MapConst', 0x40: 'EndMapConst',
    0x41: 'Vector3fConst', 0x42: 'StructMemberContext', 0x43: 'LetMulticastDelegate', 0x44: 'LetDelegate',
    0x45: 'LocalVirtualFunction', 0x46: 'LocalFinalFunction', 0x48: 'LocalOutVariable', 0x4A: 'DeprecatedOp4A',
    0x4B: 'InstanceDelegate', 0x4C: 'PushExecutionFlow', 0x4D: 'PopExecutionFlow', 0x4E: 'ComputedJump',
    0x4F: 'PopExecutionFlowIfNot', 0x50: 'Breakpoint', 0x51: 'InterfaceContext', 0x52: 'ObjToInterfaceCast',
    0x53: 'EndOfScript', 0x54: 'CrossInterfaceCast', 0x55: 'InterfaceToObjCast', 0x5A: 'WireTracepoint',
    0x5B: 'SkipOffsetConst', 0x5C: 'AddMulticastDelegate', 0x5D: 'ClearMulticastDelegate', 0x5E: 'Tracepoint',
    0x5F: 'LetObj', 0x60: 'LetWeakObjPtr', 0x61: 'BindDelegate', 0x62: 'RemoveMulticastDelegate',
    0x63: 'CallMulticastDelegate', 0x64: 'LetValueOnPersistentFrame', 0x65: 'ArrayConst', 0x66: 'EndArrayConst',
    0x67: 'SoftObjectConst', 0x68: 'CallMath', 0x69: 'SwitchValue', 0x6A: 'InstrumentationEvent',
    0x6B: 'ArrayGetByRef', 0x6C: 'ClassSparseDataVariable', 0x6D: 'FieldPathConst',
}
NOARG = {'Nothing', 'EndOfScript', 'EndFunctionParms', 'EndStructConst', 'EndArray', 'EndArrayConst', 'EndSet',
         'EndMap', 'EndSetConst', 'EndMapConst', 'IntZero', 'IntOne', 'True', 'False', 'NoObject', 'NoInterface',
         'Self', 'EndParmValue', 'PopExecutionFlow', 'DeprecatedOp4A', 'Tracepoint', 'WireTracepoint', 'Breakpoint'}
OPS = {
    'EqualEqual': '==', 'NotEqual': '!=', 'Greater': '>', 'GreaterEqual': '>=', 'Less': '<', 'LessEqual': '<=',
    'Add': '+', 'Subtract': '-', 'Multiply': '*', 'Divide': '/', 'BooleanAND': '&&', 'BooleanOR': '||',
}


class Kismet:
    """Decodes serialized script into an expression tree, tracking in-memory offsets for jump targets."""

    def __init__(self, asset, data, name_mem=12):
        self.a = asset
        self.r = R(data)
        self.mem = 0
        self.NAME_MEM = name_mem

    def u8(self):
        self.mem += 1; return self.r.u8()

    def u16(self):
        self.mem += 2; return self.r.u16()

    def u32(self):
        self.mem += 4; return self.r.u32()

    def i32(self):
        self.mem += 4; return self.r.i32()

    def name(self):
        self.mem += self.NAME_MEM; return self.a.fname(self.r)

    def obj(self):
        self.mem += 8; return self.a.obj_name(self.r.i32())

    def prop(self):
        self.mem += 8
        n = self.r.i32()
        path = [self.a.fname(self.r) for _ in range(n)]
        owner = self.r.i32()
        return '.'.join(path) if path else 'None'

    def expr(self):
        off = self.mem
        tok = self.u8()
        op = EX.get(tok)
        if op is None:
            raise ValueError(f'unknown token {tok:#x} at mem {off}')
        n = dict(op=op, off=off)
        if op in NOARG:
            pass
        elif op in ('LocalVariable', 'InstanceVariable', 'DefaultVariable', 'LocalOutVariable', 'ClassSparseDataVariable', 'PropertyConst'):
            n['prop'] = self.prop()
        elif op == 'NothingInt32':
            n['v'] = self.i32()
        elif op in ('Return', 'InterfaceContext', 'SoftObjectConst', 'FieldPathConst', 'ComputedJump', 'PopExecutionFlowIfNot', 'ClearMulticastDelegate'):
            n['e'] = self.expr()
        elif op == 'Jump' or op == 'PushExecutionFlow' or op == 'SkipOffsetConst':
            n['to'] = self.u32()
        elif op == 'JumpIfNot':
            n['to'] = self.u32(); n['e'] = self.expr()
        elif op == 'Assert':
            self.u16(); self.u8(); n['e'] = self.expr()
        elif op == 'Let':
            n['prop'] = self.prop(); n['var'] = self.expr(); n['val'] = self.expr()
        elif op in ('LetObj', 'LetWeakObjPtr', 'LetBool', 'LetDelegate', 'LetMulticastDelegate', 'AddMulticastDelegate', 'RemoveMulticastDelegate', 'ArrayGetByRef'):
            n['var'] = self.expr(); n['val'] = self.expr()
        elif op == 'LetValueOnPersistentFrame':
            n['prop'] = self.prop(); n['val'] = self.expr()
        elif op == 'BitFieldConst':
            n['prop'] = self.prop(); n['v'] = self.u8()
        elif op in ('ClassContext', 'Context', 'Context_FailSilent'):
            n['obj'] = self.expr(); self.u32(); self.prop(); n['ctx'] = self.expr()
        elif op in ('MetaCast', 'DynamicCast', 'ObjToInterfaceCast', 'CrossInterfaceCast', 'InterfaceToObjCast'):
            n['cls'] = self.obj(); n['e'] = self.expr()
        elif op == 'Cast':
            n['kind'] = self.u8(); n['e'] = self.expr()
        elif op == 'Skip':
            self.u32(); n['e'] = self.expr()
        elif op in ('VirtualFunction', 'LocalVirtualFunction'):
            n['fn'] = self.name(); n['args'] = self.params()
        elif op in ('FinalFunction', 'LocalFinalFunction', 'CallMath'):
            n['fn'] = self.obj(); n['args'] = self.params()
        elif op == 'CallMulticastDelegate':
            n['fn'] = self.obj(); n['del'] = self.expr(); n['args'] = self.params()
        elif op == 'IntConst':
            n['v'] = self.i32()
        elif op == 'FloatConst':
            self.mem += 4; n['v'] = round(self.r.f32(), 4)
        elif op == 'DoubleConst':
            self.mem += 8; n['v'] = round(self.r.f64(), 4)
        elif op in ('Int64Const', 'UInt64Const'):
            self.mem += 8; n['v'] = self.r.i64()
        elif op in ('ByteConst', 'IntConstByte'):
            n['v'] = self.u8()
        elif op == 'StringConst':
            s = bytearray()
            while True:
                c = self.u8()
                if c == 0:
                    break
                s.append(c)
            n['v'] = s.decode('latin-1')
        elif op == 'UnicodeStringConst':
            s = bytearray()
            while True:
                c = self.u16()
                if c == 0:
                    break
                s += struct.pack('<H', c)
            n['v'] = s.decode('utf-16-le', 'replace')
        elif op == 'ObjectConst':
            n['v'] = self.obj()
        elif op == 'NameConst':
            n['v'] = self.name()
        elif op in ('RotationConst', 'VectorConst'):
            self.mem += 24; n['v'] = tuple(round(x, 2) for x in struct.unpack('<ddd', self.r.raw(24)))
        elif op == 'Vector3fConst':
            self.mem += 12; n['v'] = tuple(round(x, 2) for x in struct.unpack('<fff', self.r.raw(12)))
        elif op == 'TransformConst':
            self.mem += 80; self.r.raw(80); n['v'] = 'Transform(...)'
        elif op == 'TextConst':
            t = self.u8()
            if t == 0:
                n['v'] = ''
            elif t == 1:
                n['v'] = [self.expr(), self.expr(), self.expr()]
            elif t in (2, 3):
                n['v'] = [self.expr()]
            elif t == 4:
                self.obj(); n['v'] = [self.expr(), self.expr()]
        elif op == 'StructConst':
            n['struct'] = self.obj(); self.i32(); n['args'] = self.until('EndStructConst')
        elif op == 'SetArray':
            n['var'] = self.expr(); n['args'] = self.until('EndArray')
        elif op == 'ArrayConst':
            self.prop(); self.i32(); n['args'] = self.until('EndArrayConst')
        elif op in ('SetSet', 'SetMap'):
            n['var'] = self.expr(); self.i32(); n['args'] = self.until('EndSet' if op == 'SetSet' else 'EndMap')
        elif op == 'SetConst':
            self.prop(); self.i32(); n['args'] = self.until('EndSetConst')
        elif op == 'MapConst':
            self.prop(); self.prop(); self.i32(); n['args'] = self.until('EndMapConst')
        elif op == 'StructMemberContext':
            n['prop'] = self.prop(); n['e'] = self.expr()
        elif op == 'InstanceDelegate':
            n['v'] = self.name()
        elif op == 'BindDelegate':
            n['fn'] = self.name(); n['del'] = self.expr(); n['obj'] = self.expr()
        elif op == 'SwitchValue':
            cnt = self.u16(); self.u32(); n['idx'] = self.expr(); cases = []
            for _ in range(cnt):
                k = self.expr(); self.u32(); v = self.expr(); cases.append((k, v))
            n['cases'] = cases; n['default'] = self.expr()
        elif op == 'InstrumentationEvent':
            t = self.u8()
            if t == 4:
                self.name()
        else:
            raise ValueError('unhandled ' + op)
        return n

    def params(self):
        return self.until('EndFunctionParms')

    def until(self, endop):
        out = []
        while True:
            e = self.expr()
            if e['op'] == endop:
                return out
            out.append(e)

    def all(self):
        out = []
        while self.r.p < len(self.r.d):
            out.append(self.expr())
        return out


def fmt(e):
    op = e['op']
    if op in ('LocalVariable', 'InstanceVariable', 'DefaultVariable', 'LocalOutVariable'):
        p = e['prop']
        return ('self.' if op == 'InstanceVariable' else '') + p
    if op == 'Let':
        return f"{fmt(e['var'])} = {fmt(e['val'])}"
    if op in ('LetObj', 'LetBool', 'LetWeakObjPtr', 'LetDelegate', 'LetMulticastDelegate'):
        return f"{fmt(e['var'])} = {fmt(e['val'])}"
    if op == 'LetValueOnPersistentFrame':
        return f"persist.{e['prop']} = {fmt(e['val'])}"
    if op in ('Context', 'Context_FailSilent', 'ClassContext'):
        c = fmt(e['ctx'])
        if c.startswith('self.'):
            c = c[5:]
        return f"{fmt(e['obj'])}.{c}"
    if op in ('VirtualFunction', 'LocalVirtualFunction', 'FinalFunction', 'LocalFinalFunction', 'CallMath'):
        fn = e['fn']
        args = [fmt(a) for a in e['args']]
        base = fn.split('_')[0]
        if base in OPS and len(args) == 2 and fn.count('_') <= 2:
            return f"({args[0]} {OPS[base]} {args[1]})"
        if fn == 'Not_PreBool':
            return f"!{args[0]}"
        return f"{fn}({', '.join(args)})"
    if op == 'CallMulticastDelegate':
        return f"{fmt(e['del'])}.Broadcast({', '.join(fmt(a) for a in e['args'])})"
    if op in ('IntConst', 'FloatConst', 'DoubleConst', 'ByteConst', 'IntConstByte', 'Int64Const', 'UInt64Const'):
        return str(e['v'])
    if op in ('StringConst', 'UnicodeStringConst'):
        return repr(e['v'])
    if op == 'NameConst':
        return f"Name({e['v']})"
    if op == 'ObjectConst':
        return f"<{e['v']}>"
    if op in ('VectorConst', 'RotationConst', 'Vector3fConst'):
        return f"{op[:3]}{e['v']}"
    if op == 'TextConst':
        v = e['v']
        return 'Text(' + (', '.join(fmt(x) for x in v) if isinstance(v, list) else repr(v)) + ')'
    if op == 'IntZero':
        return '0'
    if op == 'IntOne':
        return '1'
    if op == 'True':
        return 'true'
    if op == 'False':
        return 'false'
    if op in ('NoObject', 'NoInterface'):
        return 'null'
    if op == 'Self':
        return 'self'
    if op in ('MetaCast', 'DynamicCast', 'ObjToInterfaceCast', 'CrossInterfaceCast', 'InterfaceToObjCast'):
        return f"Cast<{e['cls']}>({fmt(e['e'])})"
    if op == 'Cast':
        return f"cast{e['kind']}({fmt(e['e'])})"
    if op == 'InterfaceContext':
        return fmt(e['e'])
    if op == 'StructMemberContext':
        return f"{fmt(e['e'])}.{e['prop']}"
    if op == 'StructConst':
        return f"{e['struct']}{{{', '.join(fmt(a) for a in e['args'])}}}"
    if op == 'SetArray':
        return f"{fmt(e['var'])} = [{', '.join(fmt(a) for a in e['args'])}]"
    if op in ('SetSet', 'SetMap'):
        return f"{fmt(e['var'])} = {{{', '.join(fmt(a) for a in e['args'])}}}"
    if op == 'ArrayConst':
        return f"[{', '.join(fmt(a) for a in e['args'])}]"
    if op == 'Jump':
        return f"goto L{e['to']}"
    if op == 'JumpIfNot':
        return f"if !({fmt(e['e'])}) goto L{e['to']}"
    if op == 'PushExecutionFlow':
        return f"push_flow L{e['to']}"
    if op == 'PopExecutionFlow':
        return 'pop_flow'
    if op == 'PopExecutionFlowIfNot':
        return f"pop_flow_if_not({fmt(e['e'])})"
    if op == 'ComputedJump':
        return f"goto *({fmt(e['e'])})"
    if op == 'Return':
        return f"return {fmt(e['e'])}"
    if op == 'Skip':
        return fmt(e['e'])
    if op in ('AddMulticastDelegate', 'RemoveMulticastDelegate'):
        return f"{fmt(e['var'])} {'+=' if op[0] == 'A' else '-='} {fmt(e['val'])}"
    if op == 'ClearMulticastDelegate':
        return f"{fmt(e['e'])}.Clear()"
    if op == 'BindDelegate':
        return f"{fmt(e['del'])} = bind({fmt(e['obj'])}.{e['fn']})"
    if op == 'InstanceDelegate':
        return f"delegate({e['v']})"
    if op == 'SwitchValue':
        cs = ', '.join(f"{fmt(k)}: {fmt(v)}" for k, v in e['cases'])
        return f"switch({fmt(e['idx'])}){{{cs}, default: {fmt(e['default'])}}}"
    if op == 'ArrayGetByRef':
        return f"{fmt(e['var'])}[{fmt(e['val'])}]"
    if op == 'BitFieldConst':
        return f"{e['prop']}={e['v']}"
    if op == 'PropertyConst':
        return f"&{e['prop']}"
    if op in ('SoftObjectConst', 'FieldPathConst'):
        return f"soft({fmt(e['e'])})"
    if op in ('Nothing', 'EndOfScript', 'Tracepoint', 'WireTracepoint', 'Breakpoint'):
        return ''
    return f"<{op}>"


def find_script(asset, e):
    """Locate the serialized script inside a UFunction export via its size header."""
    d = asset.d
    start = e['offset']; end = start + e['size']
    cands = []
    for tail in (12, 14):
        for p in range(start, end - 8 - tail + 1):
            disk = end - tail - 8 - p
            if struct.unpack_from('<i', d, p + 4)[0] == disk:
                mem = struct.unpack_from('<i', d, p)[0]
                if 0 <= mem:
                    cands.append((mem, d[p + 8:p + 8 + disk]))
    for mem, script in cands:
        try:
            k = Kismet(asset, script)
            k.all()
            if k.mem == mem:
                return mem, script
        except Exception:
            pass
    return cands[0] if cands else (None, None)


def disasm(asset, e, entries=None):
    mem, script = find_script(asset, e)
    if script is None:
        return ['  <script not found>']
    if not script:
        return ['  <empty>']
    k = Kismet(asset, script)
    try:
        exprs = k.all()
    except Exception as ex:
        return [f'  <decode error: {ex}>']
    note = '' if k.mem == mem else f'  ; WARN mem size {k.mem} != {mem}'
    targets = set()

    def walk(x):
        if isinstance(x, dict):
            if x.get('op') in ('Jump', 'JumpIfNot', 'PushExecutionFlow'):
                targets.add(x['to'])
            for v in x.values():
                walk(v)
        elif isinstance(x, (list, tuple)):
            for v in x:
                walk(v)
    walk(exprs)
    lines = [note] if note else []
    for x in exprs:
        s = fmt(x)
        if entries and x['off'] in entries:
            lines.append(f"\n  ##### EVENT {entries[x['off']]} #####")
        if x['off'] in targets or (entries and x['off'] in entries):
            lines.append(f"L{x['off']}:")
        if s:
            lines.append('  ' + s)
    return lines


def dump(path, show_props=True, show_code=True, prop_filter=None):
    a = Asset(path)
    out = [f'=== {a.package_name}  (UE4 {a.ue4} / UE5 {a.ue5})']
    entries = {}
    for i, e in enumerate(a.exports):
        if a.class_name(e) != 'Function' or a.obj_name(i + 1).startswith('ExecuteUbergraph'):
            continue
        mem, script = find_script(a, e)
        if not script:
            continue
        try:
            for x in Kismet(a, script).all():
                if x['op'] in ('LocalFinalFunction', 'FinalFunction') and x['fn'].startswith('ExecuteUbergraph') and x['args'] and x['args'][0]['op'] in ('IntConst',):
                    entries[x['args'][0]['v']] = a.obj_name(i + 1)
        except Exception:
            pass
    for i, e in enumerate(a.exports):
        cls = a.class_name(e)
        full = a.obj_name(i + 1, True)
        if cls == 'Function':
            if show_code:
                out.append(f'\n--- function {full}')
                out.extend(disasm(a, e, entries if a.obj_name(i + 1).startswith('ExecuteUbergraph') else None))
            continue
        if not show_props:
            continue
        if prop_filter and not prop_filter(cls, full):
            continue
        try:
            props = a.export_props(e)
        except Exception as ex:
            props = [('<error>', str(ex))]
        out.append(f'\n--- [{cls}] {full}')
        for k, v in props:
            sv = repr(v)
            if len(sv) > 400:
                sv = sv[:400] + '...'
            out.append(f'    {k} = {sv}')
    return '\n'.join(out)


def dump_tree(root, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    for folder, _, files in os.walk(root):
        for fn in sorted(files):
            if not fn.endswith('.uasset'):
                continue
            path = os.path.join(folder, fn)
            with open(path, 'rb') as f:
                if f.read(4) != struct.pack('<I', 0x9E2A83C1):
                    continue  # LFS pointer or not a package
            base = f"{os.path.basename(folder)}__{fn[:-len('.uasset')]}"
            for kind, kwargs in (('code', dict(show_props=False)), ('props', dict(show_code=False, prop_filter=_no_graph_nodes))):
                try:
                    text = dump(path, **kwargs)
                except Exception as ex:  # keep going on assets this reader does not understand
                    text = f'<failed to read {path}: {ex}>'
                with open(os.path.join(out_dir, f'{base}.{kind}.txt'), 'w') as out:
                    out.write(text + '\n')


def _no_graph_nodes(cls, full):
    return not cls.startswith(('K2Node', 'EdGraph')) and cls != 'EdGraph'


if __name__ == '__main__':
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    mode = sys.argv[1]
    if mode == 'tree':
        dump_tree(sys.argv[2], sys.argv[3])
        sys.exit(0)
    for p in sys.argv[2:]:
        if mode == 'code':
            print(dump(p, show_props=False))
        elif mode == 'props':
            print(dump(p, show_code=False, prop_filter=_no_graph_nodes))
        elif mode == 'all':
            print(dump(p))
        elif mode == 'exports':
            a = Asset(p)
            for i, e in enumerate(a.exports):
                print(i + 1, a.class_name(e), a.obj_name(i + 1, True))
        elif mode == 'names':
            print('\n'.join(Asset(p).names))
        else:
            sys.exit(__doc__)
