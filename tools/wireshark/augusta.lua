-- The augusta dissector (ADR-0076): Wireshark's built-in protobuf dissector
-- reads each packet of a Development build's pcapng as one augusta.Message,
-- from the protocol's one .proto on the Protobuf search path. It holds no field
-- names and parses nothing itself, so it never changes when the protocol does.
--
-- Each packet's exported_pdu TLVs name this dissector, which exported_pdu finds
-- by its Proto's name: setting a Proto's dissector registers it under that name.

local augusta = Proto("augusta", "Augusta")

local direction = ProtoField.string("augusta.direction", "Direction")
augusta.fields = { direction }

-- pinfo.p2p_dir, from the packet's epb_flags.
local directions = { [0] = "Outbound", [1] = "Inbound" }

local protobuf = Dissector.get("protobuf")

function augusta.dissector(tvb, pinfo, tree)
  pinfo.cols.protocol = "Augusta"
  local item = tree:add(augusta, tvb())
  item:add(direction, directions[pinfo.p2p_dir] or "Unknown"):set_generated()
  -- How a Lua dissector names protobuf's message type.
  pinfo.private["pb_msg_type"] = "message,augusta.Message"
  -- On the packet's tree, not the item's, so protobuf is a layer of its own.
  protobuf:call(tvb, pinfo, tree)
end
