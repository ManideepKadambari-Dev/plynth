import QtQuick
import QtQuick3D
import QtQuick3D.AssetUtils
import QtQuick3D.Helpers

// The 3D scene of the app: one model from an FBX file, lit, with a camera that orbits a target or flies.
// "scene" is the C++ ModelView that hosts it, "Theme" holds every color and number.
Item {
    id: root
    focus: true

    property real yaw: 45
    property real pitch: 25
    property real distance: 10
    property vector3d target: Qt.vector3d(0, 0, 0)
    property bool userMoved: false
    property bool flying: false
    property bool flown: false
    property real flyScale: 1
    property var heldKeys: ({})

    readonly property real fieldOfView: 40
    readonly property real fitMargin: 1.06
    readonly property real minPitch: 2
    readonly property real maxPitch: 89.9
    readonly property real orbitSpeed: 0.4
    readonly property real zoomStep: 0.88
    readonly property real fbxUnit: 0.01
    readonly property real shadowBias: 5
    readonly property real lookSpeed: 0.25
    readonly property real flyPitchLimit: 89.9
    readonly property real flySpeedPerSize: 0.4
    readonly property real flyFastFactor: 3
    readonly property real flyWheelStep: 1.25
    readonly property real minFlyScale: 0.05
    readonly property real maxFlyScale: 20
    readonly property real maxFrameTime: 0.1
    readonly property real flyNear: 0.05

    // The length of the diagonal of the building's box, which sets the fly speed.
    readonly property real buildingSize: {
        var lo = scene.boundsMin
        var hi = scene.boundsMax
        var dx = hi.x - lo.x, dy = hi.y - lo.y, dz = hi.z - lo.z
        return Math.max(1, Math.sqrt(dx * dx + dy * dy + dz * dz))
    }
    // How far the camera is from the far side of the building, so the far clip plane never cuts it.
    property real farReach: 0

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
        var lo = scene.boundsMin
        var hi = scene.boundsMax
        var c = camera.position
        var fx = Math.max(Math.abs(c.x - lo.x), Math.abs(c.x - hi.x))
        var fy = Math.max(Math.abs(c.y - lo.y), Math.abs(c.y - hi.y))
        var fz = Math.max(Math.abs(c.z - lo.z), Math.abs(c.z - hi.z))
        root.farReach = Math.sqrt(fx * fx + fy * fy + fz * fz)
    }

    // The direction the camera looks in, from the yaw and pitch.
    function forward() {
        var p = root.pitch * Math.PI / 180
        var y = root.yaw * Math.PI / 180
        var cp = Math.cos(p)
        return Qt.vector3d(-cp * Math.sin(y), -Math.sin(p), -cp * Math.cos(y))
    }

    // Turns the camera where it stands: the orbit target becomes the point the same distance in front of it.
    function look(dx, dy) {
        var eye = camera.position
        root.yaw -= dx * root.lookSpeed
        root.pitch = Math.max(-root.flyPitchLimit, Math.min(root.flyPitchLimit, root.pitch + dy * root.lookSpeed))
        var f = forward()
        root.target = Qt.vector3d(eye.x + f.x * root.distance, eye.y + f.y * root.distance, eye.z + f.z * root.distance)
        root.flown = true
        updateCamera()
    }

    // True while this key is held down.
    function held(key) {
        return root.heldKeys[key] === true
    }

    // Moves the camera for one frame by the held keys: along the view, to the side, and straight up or down.
    function flyStep(seconds) {
        var ahead = (held(Qt.Key_W) ? 1 : 0) - (held(Qt.Key_S) ? 1 : 0)
        var side = (held(Qt.Key_D) ? 1 : 0) - (held(Qt.Key_A) ? 1 : 0)
        var rise = (held(Qt.Key_E) ? 1 : 0) - (held(Qt.Key_Q) ? 1 : 0)
        if (ahead === 0 && side === 0 && rise === 0)
            return
        var step = root.buildingSize * root.flySpeedPerSize * root.flyScale * Math.min(seconds, root.maxFrameTime)
        if (held(Qt.Key_Shift))
            step *= root.flyFastFactor
        var f = forward()
        var y = root.yaw * Math.PI / 180
        var r = Qt.vector3d(Math.cos(y), 0, -Math.sin(y))
        root.target = Qt.vector3d(root.target.x + (f.x * ahead + r.x * side) * step,
                                  root.target.y + (f.y * ahead + rise) * step,
                                  root.target.z + (f.z * ahead + r.z * side) * step)
        root.userMoved = true
        root.flown = true
        updateCamera()
    }

    // Ends a flight: the camera stays where it is and the held keys are forgotten.
    function stopFlying() {
        root.flying = false
        root.heldKeys = ({})
    }

    // The keys a flight uses.
    function isFlyKey(key) {
        return key === Qt.Key_W || key === Qt.Key_A || key === Qt.Key_S || key === Qt.Key_D ||
               key === Qt.Key_Q || key === Qt.Key_E || key === Qt.Key_Shift
    }

    // Frames the whole building again from the present side, back on an orbit above it.
    function frameAll() {
        root.pitch = Math.max(root.minPitch, Math.min(root.maxPitch, root.pitch))
        fit()
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
        root.flown = false
        updateCamera()
    }

    // Sets the angle of a named view, or the free angle the host gave, then frames the model.
    function applyPreset() {
        if (scene.customAngle) {
            root.yaw = scene.angleYaw
            root.pitch = Math.max(root.minPitch, Math.min(root.maxPitch, scene.anglePitch))
        } else {
            root.yaw = scene.preset === 1 ? 0 : 45
            root.pitch = scene.preset === 1 ? root.maxPitch : 25
        }
        fit()
    }

    property var madeObjects: []
    property var partList: []
    property bool applied: false

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
        var t = textureComponent.createObject(root, {
            source: url,
            generateMipmaps: true,
            mipFilter: Texture.Linear,
            minFilter: Texture.Linear,
            magFilter: Texture.Linear
        })
        root.madeObjects.push(t)
        return t
    }

    // Makes the material of one entry of materials.json: factors are 1 because the maps carry the values, and
    // roughness, metalness, opacity and occlusion are read from the red channel of their maps.
    function makeMaterial(e) {
        var m = materialComponent.createObject(root, { baseColor: "white", roughness: 1, metalness: 1 })
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

    // Looks at every part loader: an error shows at once, and when all have loaded the built materials go on.
    function checkParts() {
        var count = partLoaders.count
        var ready = 0
        for (var i = 0; i < count; i++) {
            var item = partLoaders.objectAt(i)
            if (!item)
                continue
            if (item.status === RuntimeLoader.Error) {
                scene.reportStatus(2, item.errorString)
                return
            }
            if (item.status === RuntimeLoader.Success)
                ready++
        }
        if (count > 0 && ready === count) {
            if (!root.applied) {
                root.applied = true
                destroyMade()
                var list = scene.builtMaterials
                if (list.length > 0) {
                    for (var j = 0; j < count; j++)
                        replaceIn(partLoaders.objectAt(j), list)
                }
            }
            scene.reportStatus(1, "")
        } else {
            scene.reportStatus(0, "")
        }
    }

    // Empties every loader, which also frees the mesh data it registered, and makes fresh loaders for the parts
    // the host gives now, so a file written again at the same path is read again.
    function takeParts() {
        root.applied = false
        destroyMade()
        for (var i = 0; i < partLoaders.count; i++) {
            var item = partLoaders.objectAt(i)
            if (item)
                item.source = ""
        }
        root.partList = []
        root.partList = scene.parts
    }

    // Removes the textures and materials made for the last load.
    function destroyMade() {
        for (var i = 0; i < root.madeObjects.length; i++)
            root.madeObjects[i].destroy()
        root.madeObjects = []
    }

    // Swaps the loaded materials for the built ones. A material with no entry keeps the FBX look.
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
    Component.onCompleted: {
        root.partList = scene.parts
        applyPreset()
    }

    Connections {
        target: scene
        function onPresetChanged() { root.applyPreset() }
        function onFitRequested() { root.fit() }
        function onBoundsChanged() { root.fit() }
        function onPartsChanged() { root.takeParts() }
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
            tonemapMode: SceneEnvironment.TonemapModeFilmic
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
            lightProbe: Texture {
                textureData: ProceduralSkyTextureData {
                    skyTopColor: Theme.skyTopColor
                    skyHorizonColor: Theme.skyHorizonColor
                    groundHorizonColor: Theme.groundHorizonColor
                    groundBottomColor: Theme.groundBottomColor
                }
            }
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
            clipNear: root.flown ? root.flyNear : Math.max(root.flyNear, root.distance * 0.02)
            clipFar: Math.max(root.distance * 4, root.farReach * 1.2) + 10
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
            shadowMapFar: Math.max(root.distance * 4, root.farReach * 1.2) + 10
            shadowBias: root.shadowBias
        }

        Repeater3D {
            id: partLoaders
            model: root.partList
            RuntimeLoader {
                required property var modelData
                required property int index
                source: modelData
                visible: scene.partVisible[index] !== false
                scale: Qt.vector3d(root.fbxUnit, root.fbxUnit, root.fbxUnit)
                onStatusChanged: Qt.callLater(root.checkParts)
            }
        }
    }

    FrameAnimation {
        running: root.flying
        onTriggered: root.flyStep(frameTime)
    }

    Keys.onShortcutOverride: function(event) {
        if ((root.flying && root.isFlyKey(event.key)) || (event.key === Qt.Key_F && event.modifiers === Qt.NoModifier))
            event.accepted = true
    }
    Keys.onPressed: function(event) {
        if (root.flying && root.isFlyKey(event.key)) {
            var keys = root.heldKeys
            keys[event.key] = true
            root.heldKeys = keys
            event.accepted = true
        } else if (event.key === Qt.Key_F && event.modifiers === Qt.NoModifier && !event.isAutoRepeat
                   && scene.interactive && !root.topOrtho) {
            root.frameAll()
            event.accepted = true
        }
    }
    Keys.onReleased: function(event) {
        if (event.isAutoRepeat || !root.isFlyKey(event.key))
            return
        var keys = root.heldKeys
        delete keys[event.key]
        root.heldKeys = keys
        event.accepted = root.flying
    }
    onActiveFocusChanged: if (!activeFocus) root.stopFlying()

    MouseArea {
        anchors.fill: parent
        enabled: scene.interactive
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        property real lastX: 0
        property real lastY: 0

        onPressed: function(mouse) {
            lastX = mouse.x
            lastY = mouse.y
            root.forceActiveFocus()
            if (mouse.button === Qt.RightButton && !root.topOrtho)
                root.flying = true
        }
        onReleased: function(mouse) {
            if (mouse.button === Qt.RightButton)
                root.stopFlying()
        }
        onCanceled: root.stopFlying()
        onPositionChanged: function(mouse) {
            var dx = mouse.x - lastX
            var dy = mouse.y - lastY
            lastX = mouse.x
            lastY = mouse.y
            root.userMoved = true
            if (root.flying) {
                root.look(dx, dy)
                return
            }
            if (mouse.buttons & Qt.LeftButton) {
                root.yaw -= dx * root.orbitSpeed
                root.pitch = Math.max(Math.min(root.minPitch, root.pitch),
                                      Math.min(root.maxPitch, root.pitch + dy * root.orbitSpeed))
            } else if (mouse.buttons & Qt.MiddleButton) {
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
            if (root.flying) {
                root.flyScale = Math.max(root.minFlyScale, Math.min(root.maxFlyScale,
                                         root.flyScale * Math.pow(root.flyWheelStep, wheel.angleDelta.y / 120)))
                speedHint.show()
                return
            }
            root.distance = Math.max(0.2, Math.min(300, root.distance * Math.pow(root.zoomStep, wheel.angleDelta.y / 120)))
            root.updateCamera()
        }
    }

    Rectangle {
        id: speedHint
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 2 * Theme.unit
        width: speedText.implicitWidth + 4 * Theme.unit
        height: speedText.implicitHeight + 2 * Theme.unit
        radius: Theme.radius
        color: Theme.raised
        border.color: Theme.line
        visible: false

        // Shows the fly speed for a moment.
        function show() {
            visible = true
            hideTimer.restart()
        }

        Text {
            id: speedText
            anchors.centerIn: parent
            text: qsTr("Fly speed %1x").arg(root.flyScale.toFixed(2))
            color: Theme.text
            font.family: Theme.uiFamily
            font.pixelSize: Theme.sizeNormal
        }

        Timer {
            id: hideTimer
            interval: 1200
            onTriggered: speedHint.visible = false
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
