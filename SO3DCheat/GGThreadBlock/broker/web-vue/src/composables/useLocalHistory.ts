const MAX_ITEMS = 20

function load(key: string): string[] {
  try {
    const raw = localStorage.getItem(key)
    return raw ? JSON.parse(raw) : []
  } catch {
    return []
  }
}

function save(key: string, list: string[]) {
  localStorage.setItem(key, JSON.stringify(list))
}

export function useLocalHistory(storageKey: string) {
  const items = load(storageKey)

  function push(value: string) {
    const trimmed = value.trim()
    if (!trimmed) return
    const idx = items.indexOf(trimmed)
    if (idx !== -1) items.splice(idx, 1)
    items.unshift(trimmed)
    if (items.length > MAX_ITEMS) items.length = MAX_ITEMS
    save(storageKey, items)
  }

  function getAll(): string[] {
    return [...items]
  }

  return { push, getAll }
}
