# Serialization Format

Game-state messages use a custom lightweight binary format rather than a
schema-compiler library (e.g. Protobuf/FlatBuffers), given how small and stable
the message set is.
