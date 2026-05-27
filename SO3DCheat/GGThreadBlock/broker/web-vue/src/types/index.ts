export interface Instance {
  pid: number
  characterName: string
  hostExe: string
  money: number
  lastSeen: number
}

export interface BagItem {
  bagId: number
  slotIndex: number
  itemId: number
  count: number
  name: string
}

export interface WsMessage {
  type: string
  payload: any
}
