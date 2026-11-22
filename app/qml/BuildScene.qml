import QtQuick
import QtQuick3D
import QtQuick3D.AssetUtils
import QtQuick3D.Helpers

// The 3D scene of the app: one model from an FBX file, lit, with a camera that orbits a target.
// "scene" is the C++ ModelView that hosts it, "Theme" holds every color and number.
Item {
    id: root

    property real yaw: 45
    property real pitch: 25
    property real distance: 10
    property vector3d target: Qt.vector3d(0, 0, 0)
    property bool userMoved: false

    readonly property real fieldOfView: 40
    readonly property real fitMargin: 1.06
    readonly property real minPitch: 2
    readonly property real maxPitch: 89.9
    readonly property real orbitSpeed: 0.4
    readonly property real zoomStep: 0.88
    readonly property real fbxUnit: 0.01
    readonly property real shadowBias: 5

    readonly property bool topOrtho: scene.orthoScale > 0

    // Puts the camera on its orbit around the target. The flat top view of the plan overlay has its own camera.
    function updateCamera() {
        if (root.topOrtho) {
            var lo = scene.boundsMin
            var hi = scene.boundsMax
            orthoCamera.position = Qt.vector3d((lo.x + hi.x) / 2, hi.y + 20, (lo.z + hi.z) / 2)
            return
        }
        var p = root.pitch * Math.PI / 180
        var y = root.yaw * Math.PI / 180
        var cp = Math.cos(p)
        camera.position = Qt.vector3d(root.target.x + root.distance * cp * Math.sin(y),
                                      root.target.y + root.distance * Math.sin(p),
                                      root.target.z + root.distance * cp * Math.cos(y))
        camera.lookAt(root.target)
    }

    // Frames the whole bounding box for the current angle, as tightly as the window allows.
    function fit() {
        if (root.width <= 0 || root.height <= 0)
            return
        if (root.topOrtho) {
            updateCamera()
            return
        }
        var lo = scene.boundsMin
        var hi = scene.boundsMax
        var centre = Qt.vector3d((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2)
        var p = root.pitch * Math.PI / 180
        var y = root.yaw * Math.PI / 180
        var cp = Math.cos(p), sp = Math.sin(p), cy = Math.cos(y), sy = Math.sin(y)
        var fwd = Qt.vector3d(-cp * sy, -sp, -cp * cy)
        var right = Qt.vector3d(cy, 0, -sy)
        var up = Qt.vector3d(-sy * sp, cp, -cy * sp)
        var tanV = Math.tan(root.fieldOfView * Math.PI / 360)
        var tanH = tanV * root.width / root.height
        var need = 0.1
        for (var i = 0; i < 8; ++i) {
            var r = Qt.vector3d((i & 1 ? hi.x : lo.x) - centre.x,
                                (i & 2 ? hi.y : lo.y) - centre.y,
                                (i & 4 ? hi.z : lo.z) - centre.z)
            var along = r.x * fwd.x + r.y * fwd.y + r.z * fwd.z
            var across = Math.abs(r.x * right.x + r.y * right.y + r.z * right.z)
            var rise = Math.abs(r.x * up.x + r.y * up.y + r.z * up.z)
            need = Math.max(need, across / tanH - along, rise / tanV - along)
        }
        root.target = centre
        root.distance = need * root.fitMargin
        root.userMoved = false
        updateCamera()
    }

    // Sets the angle of a named view, then frames the model.
    function applyPreset() {
        root.yaw = scene.preset === 1 ? 0 : 45
        root.pitch = scene.preset === 1 ? root.maxPitch : 25
        fit()
    }

    property var madeObjects: []

    Component { id: textureComponent; Texture { } }
    Component { id: materialComponent; PrincipledMaterial { } }

    // The key that ties a loaded material to an entry of materials.json: the base color image path in lower
    // case with forward slashes, because the FBX loader keeps no material names.
    function pathKey(source) {
        var text = "" + source
        text = text.replace(/^file:\/\/\//, "")
        try { text = decodeURIComponent(text) } catch (e) { }
        return text.replace(/\\/g, "/").toLowerCase()
    }

    function makeTexture(url) {
        var t = textureComponent.createObject(loader, { source: url })
        root.madeObjects.push(t)
        return t
    }

    // Makes the material of one entry of materials.json: factors are 1 because the maps carry the values, and
    // roughness, metalness, opacity and occlusion are read from the red channel of their maps.
    function makeMaterial(e) {
        var m = materialComponent.createObject(loader, { baseColor: "white", roughness: 1, metalness: 1 })
        root.madeObjects.push(m)
        m.baseColorMap = makeTexture(e.baseColor)
        m.normalMap = makeTexture(e.normal)
        m.roughnessMap = makeTexture(e.roughness)
        m.roughnessChannel = Material.R
        m.metalnessMap = makeTexture(e.metallic)
        m.metalnessChannel = Material.R
        if (e.occlusion !== "") {
            m.occlusionMap = makeTexture(e.occlusion)
            m.occlusionChannel = Material.R
        }
        if (e.opacity !== "") {
            m.opacity = 1
            m.opacityMap = makeTexture(e.opacity)
            m.opacityChannel = Material.R
            if (e.blend)
                m.alphaMode = PrincipledMaterial.Blend
        }
        return m
    }

    function destroyMade() {
        for (var i = 0; i < root.madeObjects.length; i++)
            root.madeObjects[i].destroy()
        root.madeObjects = []
    }

    // Swaps the loaded materials for the built ones. A material with no entry keeps the FBX look.
    function applyMaterials(node) {
        destroyMade()
        var list = scene.builtMaterials
        if (list.length === 0)
            return
        replaceIn(node, list)
    }

    function replaceIn(node, list) {
        if (node instanceof Model) {
            var swapped = []
            for (var k = 0; k < node.materials.length; k++) {
                var old = node.materials[k]
                var entry = null
                if (old.baseColorMap) {
                    var key = pathKey(old.baseColorMap.source)
                    for (var i = 0; i < list.length; i++) {
                        if (list[i].key === key) { entry = list[i]; break }
                    }
                }
                swapped.push(entry ? makeMaterial(entry) : old)
            }
            node.materials = swapped
        }
        for (var j = 0; j < node.children.length; j++)
            replaceIn(node.children[j], list)
    }

    onWidthChanged: if (!root.userMoved) Qt.callLater(fit)
    onHeightChanged: if (!root.userMoved) Qt.callLater(fit)
    Component.onCompleted: applyPreset()

    Connections {
        target: scene
        function onPresetChanged() { root.applyPreset() }
        function onFitRequested() { root.fit() }
        function onBoundsChanged() { root.fit() }
        function onReloadRequested() {
            loader.source = ""
            Qt.callLater(function() { loader.source = scene.modelFile })
        }
    }

    // The 3D view is transparent so the Ground colour shows exactly, without the renderer's colour curve.
    Rectangle {
        anchors.fill: parent
        color: Theme.ground
        visible: !scene.transparent
    }

    View3D {
        id: view
        anchors.fill: parent
        visible: scene.modelFile !== ""

        camera: root.topOrtho ? orthoCamera : camera

        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Transparent
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
            lightProbe: Texture { textureData: ProceduralSkyTextureData {} }
            probeExposure: Theme.probeExposure
            aoEnabled: true
            aoStrength: Theme.aoStrength
            aoDistance: 0.5
            aoSoftness: 50
            aoSampleRate: 3
            aoDither: true
        }

        PerspectiveCamera {
            id: camera
            fieldOfView: root.fieldOfView
            clipNear: Math.max(0.05, root.distance * 0.02)
            clipFar: root.distance * 4 + 10
        }

        OrthographicCamera {
            id: orthoCamera
            eulerRotation: Qt.vector3d(-90, 0, 0)
            horizontalMagnification: Math.max(0.0001, scene.orthoScale)
            verticalMagnification: Math.max(0.0001, scene.orthoScale)
            clipNear: 1
            clipFar: 200
        }

        DirectionalLight {
            eulerRotation: Qt.vector3d(-48, 28, 0)
            color: Theme.lightColor
            brightness: Theme.keyLightBrightness
            castsShadow: scene.shadows
            shadowFactor: 55
            shadowMapQuality: Light.ShadowMapQualityHigh
            softShadowQuality: Light.PCF8
            shadowMapFar: root.distance * 4 + 10
            shadowBias: root.shadowBias
        }

        RuntimeLoader {
            id: loader
            source: scene.modelFile
            scale: Qt.vector3d(root.fbxUnit, root.fbxUnit, root.fbxUnit)
            onStatusChanged: {
                if (status === RuntimeLoader.Success) {
                    root.applyMaterials(loader)
                    scene.reportStatus(1, "")
                } else if (status === RuntimeLoader.Error)
                    scene.reportStatus(2, errorString)
                else
                    scene.reportStatus(0, "")
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: scene.interactive
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        property real lastX: 0
        property real lastY: 0

        onPressed: function(mouse) {
            lastX = mouse.x
            lastY = mouse.y
        }
        onPositionChanged: function(mouse) {
            var dx = mouse.x - lastX
            var dy = mouse.y - lastY
            lastX = mouse.x
            lastY = mouse.y
            root.userMoved = true
            if (mouse.buttons & Qt.LeftButton) {
                root.yaw -= dx * root.orbitSpeed
                root.pitch = Math.max(root.minPitch, Math.min(root.maxPitch, root.pitch + dy * root.orbitSpeed))
            } else if (mouse.buttons & (Qt.MiddleButton | Qt.RightButton)) {
                var y = root.yaw * Math.PI / 180
                var p = root.pitch * Math.PI / 180
                var perPixel = 2 * root.distance * Math.tan(root.fieldOfView * Math.PI / 360) / root.height
                var cy = Math.cos(y), sy = Math.sin(y), cp = Math.cos(p), sp = Math.sin(p)
                var right = Qt.vector3d(cy, 0, -sy)
                var up = Qt.vector3d(-sy * sp, cp, -cy * sp)
                root.target = Qt.vector3d(root.target.x + (-dx * right.x + dy * up.x) * perPixel,
                                          root.target.y + (-dx * right.y + dy * up.y) * perPixel,
                                          root.target.z + (-dx * right.z + dy * up.z) * perPixel)
            }
            root.updateCamera()
        }
        onWheel: function(wheel) {
            root.userMoved = true
            root.distance = Math.max(0.2, Math.min(300, root.distance * Math.pow(root.zoomStep, wheel.angleDelta.y / 120)))
            root.updateCamera()
        }
    }

    Text {
        anchors.centerIn: parent
        width: parent.width - 4 * Theme.unit
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        visible: scene.modelFile === "" && scene.emptyText !== ""
        text: scene.emptyText
        color: Theme.muted
        font.family: Theme.uiFamily
        font.pixelSize: Theme.sizeNormal
    }

    Text {
        anchors.centerIn: parent
        width: parent.width - 8 * Theme.unit
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        visible: scene.failure !== ""
        text: scene.failure
        color: Theme.signalAmber
        font.family: Theme.uiFamily
        font.pixelSize: Theme.sizeNormal
    }
}
