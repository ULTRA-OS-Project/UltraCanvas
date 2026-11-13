# Builds Tests/fixtures/word-shapes-word.docx (python3 make-word-shapes-fixture.py
# word-shapes-word.docx): drawn shapes as Word writes them - theme colours
# through wps:style, a custom outline with an arc, a group with a picture and
# words, VML-only shapes as Word 2007 writes them, a VML picture as Word 2003
# keeps a logo. Synthetic content.
import base64, sys, zipfile
out = sys.argv[1]
png = base64.b64decode("iVBORw0KGgoAAAANSUhEUgAAADwAAAAeCAIAAAD/+uoYAAAAOklEQVR4nO3OAQkAIBAAMeOYycTGMob3MFiArX3uOOv7QDpMWlo6QFpaOkBaWjpAWlo6QFpaOmBk+gFSlRDY5nT27gAAAABJRU5ErkJggg==")
NS = ('xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" '
      'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" '
      'xmlns:wp="http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing" '
      'xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" '
      'xmlns:pic="http://schemas.openxmlformats.org/drawingml/2006/picture" '
      'xmlns:wps="http://schemas.microsoft.com/office/word/2010/wordprocessingShape" '
      'xmlns:wpg="http://schemas.microsoft.com/office/word/2010/wordprocessingGroup" '
      'xmlns:mc="http://schemas.openxmlformats.org/markup-compatibility/2006" '
      'xmlns:v="urn:schemas-microsoft-com:vml" xmlns:o="urn:schemas-microsoft-com:office:office" '
      'xmlns:w10="urn:schemas-microsoft-com:office:word" mc:Ignorable="wps wpg"')
EMU = 12700
def anchor(i, name, cx, cy, x=0, y=0, wrap="<wp:wrapTopAndBottom/>"):
    return (f'<wp:anchor distT="0" distB="0" distL="0" distR="0" simplePos="0" relativeHeight="{i}" behindDoc="0" '
            f'locked="0" layoutInCell="1" allowOverlap="1"><wp:simplePos x="0" y="0"/>'
            f'<wp:positionH relativeFrom="column"><wp:posOffset>{x}</wp:posOffset></wp:positionH>'
            f'<wp:positionV relativeFrom="paragraph"><wp:posOffset>{y}</wp:posOffset></wp:positionV>'
            f'<wp:extent cx="{cx}" cy="{cy}"/><wp:effectExtent l="0" t="0" r="0" b="0"/>{wrap}'
            f'<wp:docPr id="{i}" name="{name}"/><wp:cNvGraphicFramePr/>')
def para(text, drawing=""):
    run = f'<w:r>{drawing}</w:r>' if drawing else ''
    return f'<w:p>{run}<w:r><w:t xml:space="preserve">{text}</w:t></w:r></w:p>'
def choice(req, body):
    return f'<mc:AlternateContent><mc:Choice Requires="{req}"><w:drawing>{body}</w:drawing></mc:Choice></mc:AlternateContent>'
gwps = 'http://schemas.microsoft.com/office/word/2010/wordprocessingShape'
gwpg = 'http://schemas.microsoft.com/office/word/2010/wordprocessingGroup'
words = lambda t: f'<wps:txbx><w:txbxContent><w:p><w:r><w:t>{t}</w:t></w:r></w:p></w:txbxContent></wps:txbx>'

# 1. A rounded rectangle coloured by its theme style, with words centred in it.
cx, cy = 144*EMU, 72*EMU
s1 = choice("wps", anchor(1, "Themed", cx, cy) +
    f'<a:graphic><a:graphicData uri="{gwps}"><wps:wsp><wps:cNvSpPr/><wps:spPr>'
    f'<a:xfrm><a:off x="0" y="0"/><a:ext cx="{cx}" cy="{cy}"/></a:xfrm>'
    '<a:prstGeom prst="roundRect"><a:avLst><a:gd name="adj" fmla="val 25000"/></a:avLst></a:prstGeom></wps:spPr>'
    '<wps:style><a:lnRef idx="2"><a:schemeClr val="accent1"><a:shade val="50000"/></a:schemeClr></a:lnRef>'
    '<a:fillRef idx="1"><a:schemeClr val="accent1"/></a:fillRef><a:effectRef idx="0"><a:schemeClr val="accent1"/></a:effectRef>'
    '<a:fontRef idx="minor"><a:schemeClr val="lt1"/></a:fontRef></wps:style>' + words("White words") +
    '<wps:bodyPr anchor="ctr"/></wps:wsp></a:graphicData></a:graphic></wp:anchor>')

# 2. A custom outline: a "D" whose round side is an arc, in a darkened theme colour.
cx, cy = 72*EMU, 72*EMU
s2 = choice("wps", anchor(2, "Custom", cx, cy, x=200*EMU) +
    f'<a:graphic><a:graphicData uri="{gwps}"><wps:wsp><wps:cNvSpPr/><wps:spPr>'
    f'<a:xfrm><a:off x="0" y="0"/><a:ext cx="{cx}" cy="{cy}"/></a:xfrm>'
    '<a:custGeom><a:avLst/><a:gdLst/><a:ahLst/><a:cxnLst/><a:rect l="0" t="0" r="r" b="b"/><a:pathLst>'
    '<a:path w="100000" h="100000"><a:moveTo><a:pt x="0" y="0"/></a:moveTo><a:lnTo><a:pt x="50000" y="0"/></a:lnTo>'
    '<a:arcTo wR="50000" hR="50000" stAng="-5400000" swAng="10800000"/><a:lnTo><a:pt x="0" y="100000"/></a:lnTo><a:close/></a:path>'
    '</a:pathLst></a:custGeom><a:solidFill><a:schemeClr val="accent2"><a:lumMod val="75000"/></a:schemeClr></a:solidFill>'
    '<a:ln w="25400"><a:noFill/></a:ln></wps:spPr><wps:bodyPr/></wps:wsp></a:graphicData></a:graphic></wp:anchor>')

# 3. A group, its members' space twice its size: a picture and a box of words.
cx, cy = 216*EMU, 72*EMU
s3 = choice("wpg", anchor(3, "Letterhead group", cx, cy) +
    f'<a:graphic><a:graphicData uri="{gwpg}"><wpg:wgp><wpg:cNvGrpSpPr/><wpg:grpSpPr>'
    f'<a:xfrm><a:off x="0" y="0"/><a:ext cx="{cx}" cy="{cy}"/><a:chOff x="0" y="0"/><a:chExt cx="{2*cx}" cy="{2*cy}"/></a:xfrm></wpg:grpSpPr>'
    f'<pic:pic><pic:nvPicPr><pic:cNvPr id="10" name="Logo"/><pic:cNvPicPr/></pic:nvPicPr><pic:blipFill><a:blip r:embed="rIdLogo"/>'
    f'<a:stretch><a:fillRect/></a:stretch></pic:blipFill><pic:spPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="{120*EMU}" cy="{60*EMU}"/></a:xfrm>'
    '<a:prstGeom prst="rect"><a:avLst/></a:prstGeom></pic:spPr></pic:pic>'
    f'<wps:wsp><wps:cNvPr id="11" name="Sender"/><wps:cNvSpPr txBox="1"/><wps:spPr><a:xfrm><a:off x="{160*EMU}" y="{20*EMU}"/>'
    f'<a:ext cx="{260*EMU}" cy="{100*EMU}"/></a:xfrm><a:prstGeom prst="rect"><a:avLst/></a:prstGeom><a:noFill/><a:ln><a:noFill/></a:ln></wps:spPr>'
    + words("Sender block") + '<wps:bodyPr lIns="0" tIns="0" rIns="0" bIns="0"/></wps:wsp></wpg:wgp></a:graphicData></a:graphic></wp:anchor>')

# 4. A line drawn upwards (flipped), dotted, with an arrowhead at its start.
cx, cy = 144*EMU, 36*EMU
s4 = choice("wps", anchor(4, "Pointer", cx, cy) +
    f'<a:graphic><a:graphicData uri="{gwps}"><wps:wsp><wps:cNvCnPr/><wps:spPr>'
    f'<a:xfrm flipV="1"><a:off x="0" y="0"/><a:ext cx="{cx}" cy="{cy}"/></a:xfrm><a:prstGeom prst="straightConnector1"><a:avLst/></a:prstGeom>'
    '<a:ln w="19050"><a:solidFill><a:srgbClr val="7030A0"/></a:solidFill><a:prstDash val="sysDot"/><a:headEnd type="triangle"/></a:ln></wps:spPr>'
    '<wps:bodyPr/></wps:wsp></a:graphicData></a:graphic></wp:anchor>')

# 5. VML only, as Word 2007 writes: a group of a rectangle, an oval and a line.
v5 = ('<w:pict><v:group id="Diagram" style="position:absolute;margin-left:0;margin-top:6pt;width:200pt;height:100pt;'
      'mso-position-horizontal-relative:text;mso-position-vertical-relative:text" coordorigin="0,0" coordsize="1000,1000">'
      '<v:rect style="position:absolute;left:0;top:0;width:500;height:500" fillcolor="red" strokecolor="black"/>'
      '<v:oval style="position:absolute;left:500;top:500;width:500;height:500" fillcolor="#00B0F0" stroked="f"/>'
      '<v:line from="0,1000" to="1000,0" strokecolor="#0000FF" strokeweight="1pt"/>'
      '<w10:wrap type="topAndBottom"/></v:group></w:pict>')
# 6. A lone VML line, a rule across the column.
v6 = ('<w:pict><v:line id="Rule" from="0,0" to="300pt,0" strokecolor="#00B050" strokeweight="2pt" '
      'style="position:absolute;mso-position-horizontal-relative:text;mso-position-vertical-relative:text"><w10:wrap type="topAndBottom"/></v:line></w:pict>')

# 7. A rectangle Word gave words ("Add Text"): coloured by its theme style
# alone, not a plain text box.
cx, cy = 144*EMU, 36*EMU
s7 = choice("wps", anchor(7, "Labelled", cx, cy) +
    f'<a:graphic><a:graphicData uri="{gwps}"><wps:wsp><wps:cNvSpPr/><wps:spPr>'
    f'<a:xfrm><a:off x="0" y="0"/><a:ext cx="{cx}" cy="{cy}"/></a:xfrm><a:prstGeom prst="rect"><a:avLst/></a:prstGeom></wps:spPr>'
    '<wps:style><a:lnRef idx="2"><a:schemeClr val="accent6"><a:shade val="50000"/></a:schemeClr></a:lnRef>'
    '<a:fillRef idx="1"><a:schemeClr val="accent6"/></a:fillRef><a:effectRef idx="0"><a:schemeClr val="accent6"/></a:effectRef>'
    '<a:fontRef idx="minor"><a:schemeClr val="lt1"/></a:fontRef></wps:style>' + words("Label") +
    '<wps:bodyPr anchor="ctr"/></wps:wsp></a:graphicData></a:graphic></wp:anchor>')

# 8. A VML picture, as Word 2003 keeps a logo: floating at the column's right.
v8 = ('<w:pict><v:shape id="Logo" type="#_x0000_t75" alt="Company logo" style="position:absolute;margin-left:0;'
      'margin-top:0;width:90pt;height:45pt;mso-position-horizontal:right;mso-position-horizontal-relative:margin;'
      'mso-position-vertical-relative:text"><v:imagedata r:id="rIdLogo" o:title="logo"/><w10:wrap type="square"/>'
      '</v:shape></w:pict>')

body = (para("Shapes as Word draws them.") + para("A themed rounded rectangle.", s1) + para("A custom outline.", s2)
        + para("A group with a picture and words.", s3) + para("A dotted pointer.", s4)
        + para("An old drawing.", v5) + para("A rule.", v6) + para("A labelled box.", s7) + para("A VML logo.", v8) + para("Closing line."))
document = (f'<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n<w:document {NS}><w:body>{body}'
            '<w:sectPr><w:pgSz w:w="11906" w:h="16838"/><w:pgMar w:top="1134" w:right="1134" w:bottom="1134" w:left="1418" '
            'w:header="709" w:footer="709" w:gutter="0"/></w:sectPr></w:body></w:document>')
theme = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n<a:theme xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" name="Test">'
         '<a:themeElements><a:clrScheme name="Test"><a:dk1><a:sysClr val="windowText" lastClr="000000"/></a:dk1>'
         '<a:lt1><a:sysClr val="window" lastClr="FFFFFF"/></a:lt1><a:dk2><a:srgbClr val="44546A"/></a:dk2><a:lt2><a:srgbClr val="E7E6E6"/></a:lt2>'
         '<a:accent1><a:srgbClr val="4472C4"/></a:accent1><a:accent2><a:srgbClr val="ED7D31"/></a:accent2><a:accent3><a:srgbClr val="A5A5A5"/></a:accent3>'
         '<a:accent4><a:srgbClr val="FFC000"/></a:accent4><a:accent5><a:srgbClr val="5B9BD5"/></a:accent5><a:accent6><a:srgbClr val="70AD47"/></a:accent6>'
         '<a:hlink><a:srgbClr val="0563C1"/></a:hlink><a:folHlink><a:srgbClr val="954F72"/></a:folHlink></a:clrScheme></a:themeElements></a:theme>')
types = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
         '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
         '<Default Extension="xml" ContentType="application/xml"/><Default Extension="png" ContentType="image/png"/>'
         '<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>'
         '<Override PartName="/word/theme/theme1.xml" ContentType="application/vnd.openxmlformats-officedocument.theme+xml"/></Types>')
rels = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
        '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/></Relationships>')
docrels = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
           '<Relationship Id="rIdTheme" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme" Target="theme/theme1.xml"/>'
           '<Relationship Id="rIdLogo" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="media/logo.png"/></Relationships>')
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for name, data in (("[Content_Types].xml", types), ("_rels/.rels", rels), ("word/document.xml", document),
                       ("word/_rels/document.xml.rels", docrels), ("word/theme/theme1.xml", theme), ("word/media/logo.png", png)):
        info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, data)
