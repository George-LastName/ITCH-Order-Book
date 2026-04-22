import { useState, useEffect, useRef } from 'react'
import { fetchTables, fetchStocks } from '../api/clickhouse.js'

const inputStyle = {
  background: '#1e2130',
  color: '#e0e0e0',
  border: '1px solid #444',
  borderRadius: 4,
  padding: '4px 8px',
  fontSize: 13,
  width: 130,
  outline: 'none',
}

const selectStyle = {
  ...inputStyle,
  width: 'auto',
  cursor: 'pointer',
}

export default function StockSelector({ onSelect }) {
  const [tables, setTables]       = useState([])
  const [table, setTable]         = useState('')
  const [allStocks, setAllStocks] = useState([])
  const [stock, setStock]         = useState('')
  const [query, setQuery]         = useState('')
  const [open, setOpen]           = useState(false)
  const [error, setError]         = useState(null)
  const wrapperRef = useRef(null)

  useEffect(() => {
    fetchTables()
      .then(rows => {
        const names = rows.map(r => r.name)
        setTables(names)
        if (names.length > 0) setTable(names[0])
      })
      .catch(e => setError(e.message))
  }, [])

  useEffect(() => {
    if (!table) return
    setAllStocks([])
    setStock('')
    setQuery('')
    fetchStocks(table)
      .then(rows => {
        const names = rows.map(r => r.stock_name)
        setAllStocks(names)
        if (names.length > 0) { setStock(names[0]); setQuery(names[0]) }
      })
      .catch(e => setError(e.message))
  }, [table])

  useEffect(() => {
    if (table && stock) onSelect(table, stock)
  }, [table, stock]) // eslint-disable-line react-hooks/exhaustive-deps

  useEffect(() => {
    function onMouseDown(e) {
      if (wrapperRef.current && !wrapperRef.current.contains(e.target)) setOpen(false)
    }
    document.addEventListener('mousedown', onMouseDown)
    return () => document.removeEventListener('mousedown', onMouseDown)
  }, [])

  const filtered = allStocks
    .filter(s => s.toLowerCase().includes(query.toLowerCase()))
    .slice(0, 50)

  function confirmStock(s) { setStock(s); setQuery(s); setOpen(false) }

  function handleKeyDown(e) {
    if (e.key === 'Enter'  && filtered.length > 0) confirmStock(filtered[0])
    if (e.key === 'Escape') setOpen(false)
  }

  if (error) return <span style={{ color: '#f87171' }}>ClickHouse error: {error}</span>

  return (
    <div style={{ display: 'flex', gap: '1.5rem', alignItems: 'center' }}>
      <label style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
        <span style={{ fontSize: 13, color: '#aaa' }}>Dataset</span>
        <select style={selectStyle} value={table} onChange={e => setTable(e.target.value)}>
          {tables.length === 0
            ? <option>Loading…</option>
            : tables.map(t => <option key={t} value={t}>{t}</option>)}
        </select>
      </label>

      <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
        <span style={{ fontSize: 13, color: '#aaa' }}>Stock</span>
        <div ref={wrapperRef} style={{ position: 'relative' }}>
          <input
            style={inputStyle}
            value={query}
            placeholder={allStocks.length === 0 ? 'Loading…' : 'Search…'}
            disabled={allStocks.length === 0}
            onChange={e => { setQuery(e.target.value); setOpen(true) }}
            onFocus={() => setOpen(true)}
            onKeyDown={handleKeyDown}
          />
          {open && filtered.length > 0 && (
            <ul style={{
              position: 'absolute', top: '100%', left: 0, zIndex: 200,
              margin: '2px 0 0', padding: 0, listStyle: 'none',
              background: '#1e2130', border: '1px solid #444', borderRadius: 4,
              width: 160, maxHeight: 220, overflowY: 'auto',
            }}>
              {filtered.map(s => (
                <li
                  key={s}
                  onMouseDown={() => confirmStock(s)}
                  style={{ padding: '5px 10px', fontSize: 13, cursor: 'pointer',
                    background: s === stock ? '#2d3555' : 'transparent', color: '#e0e0e0' }}
                  onMouseEnter={e => { e.currentTarget.style.background = '#2d3555' }}
                  onMouseLeave={e => { e.currentTarget.style.background = s === stock ? '#2d3555' : 'transparent' }}
                >
                  {s}
                </li>
              ))}
            </ul>
          )}
        </div>
        {allStocks.length > 0 && (
          <span style={{ fontSize: 12, color: '#555' }}>{allStocks.length.toLocaleString()} stocks</span>
        )}
      </div>
    </div>
  )
}
