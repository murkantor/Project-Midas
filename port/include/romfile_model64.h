#ifndef PORT_ROMFILE_MODEL64_H
#define PORT_ROMFILE_MODEL64_H
#include <bondtypes.h>
#ifdef GE_NATIVE_64
/* Convert serialized N64 nodes, textures and display lists to native layouts. */
void modelConvertN64Binary(ModelFileHeader *header, void *filedata, size_t size);
ModelNode *portModelCollisionNode(const Vertex *vertex);
void modelConvertFreeAll(void);
#endif
#endif
