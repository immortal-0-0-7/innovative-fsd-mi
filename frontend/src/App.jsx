import { useState, useEffect, useRef, Suspense, useMemo } from 'react'
import { Canvas, useFrame, useLoader } from '@react-three/fiber'
import { OrbitControls, Environment, Center } from '@react-three/drei'
import { STLLoader } from 'three/examples/jsm/loaders/STLLoader'
import * as THREE from 'three'

const POLL_INTERVAL = 62 // ~16 Hz (1000ms / 16)

// Degrees to radians
const DEG2RAD = Math.PI / 180

// CanSat 3D Model component
function CanSatModel({ orientation }) {
  const meshRef = useRef()
  const targetRotation = useRef(new THREE.Euler(0, 0, 0))

  const geometry = useLoader(STLLoader, '/cansat.stl')

  // Center and normalize the geometry on first load
  const processedGeometry = useMemo(() => {
    const geo = geometry.clone()
    geo.computeBoundingBox()
    geo.center()

    // Scale to fit nicely in the viewport
    const box = geo.boundingBox
    const maxDim = Math.max(
      box.max.x - box.min.x,
      box.max.y - box.min.y,
      box.max.z - box.min.z
    )
    const scale = 2.4 / maxDim
    geo.scale(scale, scale, scale)
    geo.computeVertexNormals()

    return geo
  }, [geometry])

  // Update target orientation when data changes
  useEffect(() => {
    if (orientation) {
      // BNO055 Euler angles: x = heading (yaw), y = roll, z = pitch
      targetRotation.current.set(
        orientation.z * DEG2RAD,  // pitch → X rotation
        orientation.x * DEG2RAD,  // heading → Y rotation
        -orientation.y * DEG2RAD  // roll → Z rotation
      )
    }
  }, [orientation])

  // Smoothly interpolate to target orientation each frame
  useFrame(() => {
    if (meshRef.current) {
      const mesh = meshRef.current
      mesh.rotation.x += (targetRotation.current.x - mesh.rotation.x) * 0.15
      mesh.rotation.y += (targetRotation.current.y - mesh.rotation.y) * 0.15
      mesh.rotation.z += (targetRotation.current.z - mesh.rotation.z) * 0.15
    }
  })

  return (
    <mesh ref={meshRef} geometry={processedGeometry} castShadow receiveShadow>
      <meshPhysicalMaterial
        color="#88b4e7"
        metalness={0.3}
        roughness={0.35}
        clearcoat={0.4}
        clearcoatRoughness={0.2}
        envMapIntensity={0.8}
      />
    </mesh>
  )
}

// Loading fallback for 3D scene
function LoadingFallback() {
  return (
    <mesh>
      <boxGeometry args={[1, 1, 1]} />
      <meshStandardMaterial color="#334155" wireframe />
    </mesh>
  )
}

function App() {
  const [data, setData] = useState(null)
  const [status, setStatus] = useState(null)
  const [connected, setConnected] = useState(false)
  const [error, setError] = useState(null)
  const [actionFeedback, setActionFeedback] = useState(null)
  const prevPacket = useRef(null)

  // Fetch sensor status once on mount
  useEffect(() => {
    fetch('/api/status')
      .then(r => r.json())
      .then(setStatus)
      .catch(() => {})
  }, [])

  // Poll /api/live at ~16 Hz
  useEffect(() => {
    let active = true

    async function poll() {
      while (active) {
        try {
          const res = await fetch('/api/live')
          const json = await res.json()
          if (active) {
            setData(json)
            setConnected(true)
            setError(null)
          }
        } catch (err) {
          if (active) {
            setConnected(false)
            setError('Cannot reach backend')
          }
        }
        await new Promise(r => setTimeout(r, POLL_INTERVAL))
      }
    }

    poll()
    return () => { active = false }
  }, [])

  // Action handler for Reset / Calibrate
  async function handleAction(endpoint, label) {
    try {
      setActionFeedback(`${label}...`)
      const res = await fetch(`/api/${endpoint}`, { method: 'POST' })
      const json = await res.json()
      if (json.success) {
        setActionFeedback(`${label} ✓`)
        // Re-fetch status after calibration
        if (endpoint === 'calibrate') {
          fetch('/api/status')
            .then(r => r.json())
            .then(setStatus)
            .catch(() => {})
        }
      } else {
        setActionFeedback(`${label} failed`)
      }
    } catch {
      setActionFeedback(`${label} — unreachable`)
    }
    setTimeout(() => setActionFeedback(null), 2500)
  }

  // Detect packet change for pulse
  const packetChanged = data && prevPacket.current !== data.p
  if (data) prevPacket.current = data.p

  const cards = data ? [
    { label: 'Packet #', value: data.p, unit: '' },
    { label: 'Pressure', value: data.pr?.toFixed(2), unit: 'hPa' },
    { label: 'Altitude', value: data.a?.toFixed(2), unit: 'm' },
    { label: 'Velocity', value: data.v?.toFixed(2), unit: 'm/s' },
    { label: 'Orientation X', value: data.x?.toFixed(2), unit: '°' },
    { label: 'Orientation Y', value: data.y?.toFixed(2), unit: '°' },
    { label: 'Orientation Z', value: data.z?.toFixed(2), unit: '°' },
    { label: 'Battery', value: data.bv?.toFixed(3), unit: 'V' },
  ] : []

  const orientation = data ? { x: data.x || 0, y: data.y || 0, z: data.z || 0 } : null

  return (
    <div className="dashboard">
      <div className="header">
        <h1>CanSat Telemetry</h1>
        <div className="subtitle">ESP32-S3 • 16 Hz Live</div>
      </div>

      {status && (
        <div className="status-bar">
          <div className="status-chip">
            <span className={`status-dot ${status.bmp ? 'ok' : 'fail'}`} />
            BMP390
          </div>
          <div className="status-chip">
            <span className={`status-dot ${status.bno ? 'ok' : 'fail'}`} />
            BNO055
          </div>
          <div className="status-chip">
            <span className={`status-dot ${status.ina ? 'ok' : 'fail'}`} />
            INA219
          </div>
          <div className="status-chip">
            REF {status.ref?.toFixed(2)} hPa
          </div>
        </div>
      )}

      {data ? (
        <div className="grid">
          {cards.map(c => (
            <div className={`card ${c.label === 'Packet #' && packetChanged ? 'pulsing' : ''}`} key={c.label}>
              <div className="card-label">{c.label}</div>
              <div className="card-value">
                {c.value}<span className="card-unit">{c.unit}</span>
              </div>
            </div>
          ))}
        </div>
      ) : (
        <div className="conn-status error">
          {error || 'Connecting to ESP32...'}
        </div>
      )}

      {/* 3D Orientation Viewer */}
      <div className="viewer-section">
        <div className="viewer-header">
          <div className="viewer-title">3D Orientation</div>
          <div className="viewer-angles">
            {orientation ? (
              <>
                <span className="angle-chip x">X {orientation.x.toFixed(1)}°</span>
                <span className="angle-chip y">Y {orientation.y.toFixed(1)}°</span>
                <span className="angle-chip z">Z {orientation.z.toFixed(1)}°</span>
              </>
            ) : (
              <span className="angle-chip">—</span>
            )}
          </div>
        </div>

        <div className="viewer-container">
          <Canvas
            camera={{ position: [3, 2, 3], fov: 40 }}
            gl={{ antialias: true, alpha: true }}
            style={{ background: 'transparent' }}
          >
            <ambientLight intensity={0.4} />
            <directionalLight position={[5, 5, 5]} intensity={1.2} castShadow />
            <directionalLight position={[-3, -1, -3]} intensity={0.3} color="#a78bfa" />
            <pointLight position={[0, 3, 0]} intensity={0.5} color="#60a5fa" />

            <Suspense fallback={<LoadingFallback />}>
              <Center>
                <CanSatModel orientation={orientation} />
              </Center>
              <Environment preset="city" />
            </Suspense>

            {/* Reference axes helper — subtle */}
            <axesHelper args={[1.5]} />
            <gridHelper args={[4, 8, '#1e293b', '#1e293b']} position={[0, -1.5, 0]} />

            <OrbitControls
              enablePan={false}
              enableZoom={true}
              minDistance={2}
              maxDistance={8}
              autoRotate={false}
            />
          </Canvas>
        </div>
      </div>

      {/* Action Buttons */}
      <div className="action-bar">
        <button
          className="action-btn reset-btn"
          onClick={() => handleAction('reset', 'Reset')}
          title="Reset packet counter on ESP32"
        >
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
            <path d="M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8" />
            <path d="M3 3v5h5" />
          </svg>
          Reset
        </button>
        <button
          className="action-btn calibrate-btn"
          onClick={() => handleAction('calibrate', 'Calibrate')}
          title="Calibrate sensors (pressure + orientation reference)"
        >
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
            <circle cx="12" cy="12" r="3" />
            <path d="M12 1v2M12 21v2M4.22 4.22l1.42 1.42M18.36 18.36l1.42 1.42M1 12h2M21 12h2M4.22 19.78l1.42-1.42M18.36 5.64l1.42-1.42" />
          </svg>
          Calibrate
        </button>
      </div>

      {actionFeedback && (
        <div className="action-feedback">{actionFeedback}</div>
      )}

      <div className={`conn-status ${connected ? 'connected' : 'error'}`}>
        {connected ? '● LIVE' : '○ DISCONNECTED'}
      </div>
    </div>
  )
}

export default App
