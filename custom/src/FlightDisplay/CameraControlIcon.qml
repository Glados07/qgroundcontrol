/****************************************************************************
 * Optical-size camera pictograms: balanced 24-unit silhouettes, rounded
 * junctions and a consistent stroke, independent of fonts/platform themes.
 ****************************************************************************/
import QtQuick

Canvas {
    id: root
    property string kind: "camera"
    property color color: "#152733"
    implicitWidth: 24
    implicitHeight: 24
    antialiasing: true
    onKindChanged: requestPaint()
    onColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        const c = getContext("2d")
        c.reset()
        c.scale(width / 24, height / 24)
        c.strokeStyle = color
        c.fillStyle = color
        c.lineWidth = 1.7
        c.lineCap = "round"
        c.lineJoin = "round"
        function path(points, close, fill) {
            c.beginPath(); c.moveTo(points[0][0], points[0][1])
            for (let i = 1; i < points.length; ++i) c.lineTo(points[i][0], points[i][1])
            if (close) c.closePath()
            if (fill) c.fill(); else c.stroke()
        }
        function circle(x, y, radius, fill) {
            c.beginPath(); c.arc(x, y, radius, 0, Math.PI * 2)
            if (fill) c.fill(); else c.stroke()
        }
        function roundedBox(x, y, w, h, r, fill) {
            c.beginPath(); c.moveTo(x+r,y); c.lineTo(x+w-r,y)
            c.quadraticCurveTo(x+w,y,x+w,y+r); c.lineTo(x+w,y+h-r)
            c.quadraticCurveTo(x+w,y+h,x+w-r,y+h); c.lineTo(x+r,y+h)
            c.quadraticCurveTo(x,y+h,x,y+h-r); c.lineTo(x,y+r)
            c.quadraticCurveTo(x,y,x+r,y); c.closePath()
            if (fill) c.fill(); else c.stroke()
        }
        switch (kind) {
        case "camera":
            c.beginPath(); c.moveTo(5,7); c.lineTo(7.2,7); c.lineTo(8.7,4.8)
            c.quadraticCurveTo(9,4.3,9.8,4.3); c.lineTo(14.2,4.3)
            c.quadraticCurveTo(15,4.3,15.3,4.8); c.lineTo(16.8,7); c.lineTo(19,7)
            c.quadraticCurveTo(21,7,21,9); c.lineTo(21,17.8)
            c.quadraticCurveTo(21,19.8,19,19.8); c.lineTo(5,19.8)
            c.quadraticCurveTo(3,19.8,3,17.8); c.lineTo(3,9)
            c.quadraticCurveTo(3,7,5,7); c.closePath(); c.stroke()
            circle(12,13.2,3.7,false); circle(18,9.8,0.65,true); break
        case "focus":
            c.beginPath(); c.moveTo(8,3.5); c.lineTo(5.5,3.5); c.quadraticCurveTo(3.5,3.5,3.5,5.5); c.lineTo(3.5,8)
            c.moveTo(16,3.5); c.lineTo(18.5,3.5); c.quadraticCurveTo(20.5,3.5,20.5,5.5); c.lineTo(20.5,8)
            c.moveTo(3.5,16); c.lineTo(3.5,18.5); c.quadraticCurveTo(3.5,20.5,5.5,20.5); c.lineTo(8,20.5)
            c.moveTo(16,20.5); c.lineTo(18.5,20.5); c.quadraticCurveTo(20.5,20.5,20.5,18.5); c.lineTo(20.5,16); c.stroke()
            roundedBox(8.5,8.5,7,7,1.5,false); break
        case "center":
            circle(12,12,6.2,false); circle(12,12,1.5,true)
            path([[12,2.8],[12,5.8]],false,false); path([[12,18.2],[12,21.2]],false,false)
            path([[2.8,12],[5.8,12]],false,false); path([[18.2,12],[21.2,12]],false,false); break
        case "record": circle(12,12,7.8,false); circle(12,12,2.3,true); break
        case "recordStop": circle(12,12,8.5,false); roundedBox(8,8,8,8,1.3,true); break
        case "stop": roundedBox(5.5,5.5,13,13,2.5,false); break
        case "sdCard":
            c.beginPath(); c.moveTo(6,3); c.lineTo(14,3); c.lineTo(19,8); c.lineTo(19,19)
            c.quadraticCurveTo(19,21,17,21); c.lineTo(7,21)
            c.quadraticCurveTo(5,21,5,19); c.lineTo(5,5)
            c.quadraticCurveTo(5,3,6,3); c.closePath(); c.stroke()
            path([[9,5.5],[9,9]],false,false); path([[12,5.5],[12,9]],false,false)
            roundedBox(8,13,8,5,1,false); break
        case "switch":
            c.beginPath(); c.moveTo(4,9); c.lineTo(4,7.5); c.quadraticCurveTo(4,4.5,7,4.5); c.lineTo(19.5,4.5)
            c.moveTo(20,15); c.lineTo(20,16.5); c.quadraticCurveTo(20,19.5,17,19.5); c.lineTo(4.5,19.5); c.stroke()
            path([[16.5,1.8],[19.5,4.5],[16.5,7.2]],false,false)
            path([[7.5,16.8],[4.5,19.5],[7.5,22.2]],false,false)
            circle(12,12,3.2,false); break
        case "settings":
            path([[7,3.5],[7,7.1]],false,false); path([[7,12.9],[7,20.5]],false,false)
            path([[17,3.5],[17,11.1]],false,false); path([[17,16.9],[17,20.5]],false,false)
            circle(7,10,2.9,false); circle(17,14,2.9,false); break
        case "zoomIn": case "zoomOut":
            circle(10.3,10.3,6.6,false); path([[15.2,15.2],[20.5,20.5]],false,false)
            path([[7.4,10.3],[13.2,10.3]],false,false)
            if (kind === "zoomIn") path([[10.3,7.4],[10.3,13.2]],false,false)
            break
        case "close": path([[6,6],[18,18]],false,false); path([[18,6],[6,18]],false,false); break
        case "read":
            c.beginPath(); c.arc(12,12,7.3,-Math.PI * 0.16,Math.PI * 1.5); c.stroke()
            path([[9.4,2.3],[12,4.7],[9.4,7.1]],false,false); break
        case "apply": path([[4,12],[9.5,17.5],[20,6.5]],false,false); break
        case "exposure":
            circle(12,12,4,false)
            for (let i=0;i<8;++i) {
                const a = i*Math.PI/4
                path([[12+7*Math.cos(a),12+7*Math.sin(a)],[12+9.5*Math.cos(a),12+9.5*Math.sin(a)]],false,false)
            }
            break
        case "lens":
            circle(12,12,8.7,false)
            for (let i=0;i<6;++i) {
                const a=i*Math.PI/3
                path([[12+8.7*Math.cos(a),12+8.7*Math.sin(a)],
                      [12+3.3*Math.cos(a+0.8),12+3.3*Math.sin(a+0.8)],
                      [12+3.3*Math.cos(a+1.85),12+3.3*Math.sin(a+1.85)]],false,false)
            }
            break
        case "image":
            roundedBox(3,3,18,18,2.5,false); circle(16.5,7.5,1.35,true)
            path([[3,17],[8.5,11],[14,17],[17,14],[21,18]],false,false); break
        case "range":
            path([[3,8],[3,3],[8,3]],false,false); path([[16,3],[21,3],[21,8]],false,false)
            path([[3,16],[3,21],[8,21]],false,false); path([[16,21],[21,21],[21,16]],false,false)
            path([[5,12],[19,12]],false,false); path([[8,9],[5,12],[8,15]],false,false)
            path([[16,9],[19,12],[16,15]],false,false); break
        case "chevronDown": path([[6,9],[12,15],[18,9]],false,false); break
        case "payloadA8":
            path([[8,3],[16,3]],false,false); path([[12,3],[12,6]],false,false)
            roundedBox(6,6,12,12,4,false); circle(12,12,3,false)
            c.beginPath(); c.moveTo(3,10); c.lineTo(3,15)
            c.quadraticCurveTo(3,21,9,21); c.lineTo(15,21)
            c.quadraticCurveTo(21,21,21,15); c.lineTo(21,10); c.stroke(); break
        case "payloadRouke":
            roundedBox(3,6.5,18,13.5,2.5,false)
            path([[6,6.5],[6,3.5],[11,3.5],[13,6.5]],false,false)
            circle(14,13,3.7,false); path([[6,10],[6,16]],false,false); break
        case "warning":
            path([[12,3],[22,21],[2,21]],true,false)
            path([[12,9],[12,14]],false,false); circle(12,17.5,0.7,true); break
        }
    }
}
