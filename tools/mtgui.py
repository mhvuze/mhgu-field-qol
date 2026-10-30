"""MHGU (version 141077) GUI files: a reader for the records Field QoL edits, after Fexty12573/mhw-gui-editor."""
import struct

HDR_COUNTS = ['animationNum', 'sequenceNum', 'objectNum', 'objSequenceNum', 'initParamNum', 'paramNum', 'keyNum',
              'instanceNum', 'flowNum', 'flowProcessNum', 'flowInputNum', 'flowSwitchNum', 'flowFunctionNum',
              'actionNum', 'inputConditionNum', 'switchConditionNum', 'switchOperatorNum', 'variableNum',
              'textureNum', 'fontNum', 'fontFilterNum', 'messageNum', 'guiResourceNum', 'generalResourceNum',
              'cameraSettingNum', 'instExeParamNum', 'vertexBufferSize', 'optionBitFlag', 'viewSize_Width',
              'viewSize_Height', 'startFlowIndex']
HDR_OFFS = ['animationOffset', 'sequenceOffset', 'objectOffset', 'objSequenceOffset', 'initParamOffset',
            'paramOffset', 'instanceOffset', 'flowOffset', 'flowProcessOffset', 'flowInputOffset',
            'flowSwitchOffset', 'flowFunctionOffset', 'actionOffset', 'inputConditionOffset',
            'switchOperatorOffset', 'switchConditionOffset', 'variableOffset', 'textureOffset', 'fontOffset',
            'fontFilterOffset', 'messageOffset', 'guiResourceOffset', 'generalResourceOffset',
            'cameraSettingOffset', 'stringOffset', 'keyOffset', 'keyValue8Offset', 'keyValue32Offset',
            'keyValue128Offset', 'extendDataOffset', 'instExeParamOffset', 'vertexOffset']
# fileType, guiVersion, fileSize, attr, revisionDate(u64), instanceID, flowID, variableID, startInstanceIndex
HDR_FMT = '<4sIIIQIIII' + 'I' * len(HDR_COUNTS) + 'I' * len(HDR_OFFS)
HDR_NAMES = ['fileType', 'guiVersion', 'fileSize', 'attr', 'revisionDate', 'instanceID', 'flowID', 'variableID',
             'startInstanceIndex'] + HDR_COUNTS + HDR_OFFS

OBJ_TYPES = {473969240: 'Root', 122938906: 'ColorAdjust', 351760238: '2D', 229195642: 'ScissorMask',
             796866380: 'Null', 663214884: 'ChildAnimationRoot', 1587625923: 'Polygon', 459477923: 'Texture',
             1332881660: 'TextureSet', 1601273156: 'Material', 800599726: 'Message', 826240196: 'Text',
             1344422210: 'Effect', 691640339: 'HitRect', 304564904: 'SizeAdjustMessage',
             1916600852: 'iAnimVariable', 2084773514: 'iAnimControl', 1923362099: 'iRoot', 1100687399: 'iNull',
             1882551812: 'iScissorMask', 497711579: 'iAnimation', 981162009: 'iGauge', 1252007991: 'iAutoAnimation',
             1235707108: 'iInput', 1786326969: 'iButtonList', 2133789891: 'iScrollBar', 579080650: 'iSlider',
             1608217519: 'iText', 9506454: 'iButtonGrid', 546128602: 'iButtonGridLink', 428837245: 'iButtonTree',
             1108543649: 'iChangeNumInput', 1352762334: 'iColorPallet', 675048259: 'iCursor',
             524580423: 'iFreeCursor', 1827074792: 'iItemGrid', 17244926: 'iMessage', 47863894: 'iMouseOverFilter'}

# record layouts: (struct format, field names)
REC = {
    'anim': ('<IHHHHIII', 'ID ObjectNum SequenceNum DrawableObjectNum AnimateParamNum RootObjectIndex name SequenceIndex'),
    'seq': ('<III', 'ID FrameCount name'),
    'obj': ('<IBB2xiiIIIIi', 'ID InitParamNum AnimateParamNum NextIndex ChildIndex name Type InitParamIndex ObjectSequenceIndex ExtendDataOffset'),
    'objseq': ('<HBBhhII', 'Attr InitParamNum ParamNum LoopStart FrameCount InitParamIndex ParamIndex'),
    'init': ('<IBB6xII', 'setter Type Use name value'),
    'param': ('<IBB2xIIII', 'setter Type ValueCount ParentID name KeyIndex value'),
    'inst': ('<IIiiIIIIi', 'ID Attr NextIndex ChildIndex InitParamNum name Type InitParamIndex ExtendDataOffset'),
    'key': ('<II', 'Data CurveOffset'),
}
SECTIONS = {'anim': ('animationOffset', 'animationNum'), 'seq': ('sequenceOffset', 'sequenceNum'),
            'obj': ('objectOffset', 'objectNum'), 'objseq': ('objSequenceOffset', 'objSequenceNum'),
            'init': ('initParamOffset', 'initParamNum'), 'param': ('paramOffset', 'paramNum'),
            'inst': ('instanceOffset', 'instanceNum'), 'key': ('keyOffset', 'keyNum')}


class Gui:
    def __init__(self, data):
        self.data = bytearray(data)
        self.h = dict(zip(HDR_NAMES, struct.unpack_from(HDR_FMT, data, 0)))
        assert self.h['fileType'] == b'GUI\0' and self.h['guiVersion'] == 141077, 'not an MHGU GUI file'
        self.rec = {}
        for k, (off, num) in SECTIONS.items():
            fmt, names = REC[k]
            size = struct.calcsize(fmt)
            base = self.h[off]
            self.rec[k] = [dict(zip(names.split(), struct.unpack_from(fmt, data, base + i * size)), _at=base + i * size)
                           for i in range(self.h[num])]

    def s(self, off):
        a = self.h['stringOffset'] + off
        return self.data[a:self.data.index(b'\0', a)].decode('utf-8', 'replace')

    def u32(self, a): return struct.unpack_from('<I', self.data, a)[0]

    def init_value(self, p):
        t, v = p['Type'], p['value']
        if t == 3: return self.data[self.h['keyValue8Offset'] + v]
        if t in (17, 18): return v
        if t in (2, 15): return struct.unpack_from('<f', self.data, self.h['keyValue32Offset'] + v)[0]
        if t == 4: return struct.unpack_from('<4f', self.data, self.h['keyValue128Offset'] + v)
        if t == 6: return self.s(struct.unpack_from('<q', self.data, self.h['keyValue32Offset'] + v)[0])
        return self.u32(self.h['keyValue32Offset'] + v)

    def inits(self, thing):
        return [(self.s(p['name']), p['Type'], self.init_value(p))
                for p in self.rec['init'][thing['InitParamIndex']:thing['InitParamIndex'] + thing['InitParamNum']]]

    def type_name(self, t): return OBJ_TYPES.get(t, hex(t))
