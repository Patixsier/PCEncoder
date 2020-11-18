#include <Slice.h>
#include <queue>
#include <array>

void Slice::encode() {
	if (points.empty())
		return;

	EncodeTreeNode headNode;
	headNode.origin = origin;
	headNode.edgeLength = edgeLength;
	headNode.level = _tzcnt_u32(edgeLength) - clipDepth;  // ��������ʱlevel�ݼ�������ʱǿ��ֹͣ
	// ��һ����Ƭ�ڿ�ͷ�ͱ�������
	if (headNode.level < 0) {
		points.clear();
		return;
	}

	headNode.sliceDataIndex.resize(points.size());
	for (int i = 0; i < points.size(); ++i) {
		headNode.sliceDataIndex[i] = i;
	}

	std::queue<EncodeTreeNode> q;
	q.push(headNode);
	do {
		const auto& node = q.front();
		auto halfLength = node.edgeLength / 2;
		auto center = node.origin + halfLength;
		std::array<std::vector<int>, 8> subSlicesDataIndex;
		for (int index : node.sliceDataIndex) {
			Vec3i32 diff = points[index].position - center;
			int subIndex;
			if (diff.x < 0) {
				if (diff.y < 0) {
					if (diff.z < 0)
						subIndex = 0;
					else
						subIndex = 1;
				}
				else {
					if (diff.z < 0)
						subIndex = 2;
					else
						subIndex = 3;
				}
			}
			else {
				if (diff.y < 0) {
					if (diff.z < 0)
						subIndex = 4;
					else
						subIndex = 5;
				}
				else {
					if (diff.z < 0)
						subIndex = 6;
					else
						subIndex = 7;
				}
			}
			subSlicesDataIndex[subIndex].push_back(index);
		}

		int controlByte = 0;
		for (int i = 0; i < 8; ++i) {
			controlByte <<= 1;
			if (!subSlicesDataIndex[i].empty()) {
				controlByte |= 1;
				if (node.level > 1) {
					EncodeTreeNode nextNode;
					nextNode.origin = Vec3i32((i & 4 ? center.x : node.origin.x),
											  (i & 2 ? center.y : node.origin.y),
											  (i & 1 ? center.z : node.origin.z));
					nextNode.edgeLength = halfLength;
					nextNode.level = node.level - 1;
					nextNode.sliceDataIndex = subSlicesDataIndex[i];
					q.push(nextNode);
				}
				else {
					// �����ڶ����Ѿ��õ�Ҷ�Ӳ�ṹ������Ҫд�������
					// ���������Ϣ
					int sum = 0;
					int num = subSlicesDataIndex[i].size();
					for (int index : subSlicesDataIndex[i]) {
						sum += points[index].color.x;
					}
					encodedColor.push_back(lroundf((float)sum / num));

					// ������ɫ�Ȳ���ʱ��������һͬ��ȡɫ��
					if (!isChromaSubsampling) {
						int sum1 = 0, sum2 = 0;
						int num = node.sliceDataIndex.size();
						for (int index : node.sliceDataIndex) {
							sum1 += points[index].color.y;
							sum2 += points[index].color.z;
						}
						encodedColor.push_back(lroundf((float)sum1 / num));
						encodedColor.push_back(lroundf((float)sum2 / num));
					}
				}
			}
			else
				controlByte |= 0;
		}
		encodedTree.push_back(controlByte);

		// �����ڶ��㣬����ɫ�Ȳ���
		if (isChromaSubsampling && node.level == 1) {
			int sum1 = 0, sum2 = 0;
			int num = node.sliceDataIndex.size();
			for (int index : node.sliceDataIndex) {
				sum1 += points[index].color.y;
				sum2 += points[index].color.z;
			}
			encodedColor.push_back(lroundf((float)sum1 / num));
			encodedColor.push_back(lroundf((float)sum2 / num));
		}

		q.pop();
	} while (!q.empty());
}

std::vector<Point> Slice::decode() {
	int level = _tzcnt_u32(edgeLength) - clipDepth;
	std::queue<DecodeTreeNode> q;
	q.push({ origin, edgeLength, level });
	int treeIndex = 0;
	int colorIndex = 0;
	do {
		const auto& node = q.front();
		int controlByte = encodedTree[treeIndex];
		treeIndex++;
		// �����ڶ����ȡ��ɫ
		if (node.level == 1) {
			// ���ȶ�ȡ����
			std::vector<Point> buffer;
			for (int i = 0; i < 8; ++i) {
				if (controlByte & 0x80) {
					int halfLength = node.edgeLength / 2;
					auto center = node.origin + halfLength;
					auto subNodeOrigin = Vec3i32((i & 4 ? center.x : node.origin.x),
												 (i & 2 ? center.y : node.origin.y),
												 (i & 1 ? center.z : node.origin.z));

					// �������ʱӦ���������ڵ��Ӧ�Ŀ�����ʾ����������һ����...
					uint8_t luma = encodedColor[colorIndex];
					colorIndex++;
					uint8_t chroma1 = 0;
					uint8_t chroma2 = 0;
					if (!isChromaSubsampling) {
						chroma1 = encodedColor[colorIndex];
						chroma2 = encodedColor[colorIndex + 1];
						colorIndex += 2;
					}
					for (int x = 0; x < halfLength; ++x) {
						for (int y = 0; y < halfLength; ++y) {
							for (int z = 0; z < halfLength; ++z) {
								buffer.push_back({ subNodeOrigin + Vec3i32(x, y, z), Vec3u8(luma, chroma1, chroma2) });
							}
						}
					}
				}
				controlByte <<= 1;
			}

			if (isChromaSubsampling) {
				uint8_t chroma1 = encodedColor[colorIndex];
				uint8_t chroma2 = encodedColor[colorIndex + 1];
				colorIndex += 2;
				for (auto& p : buffer) {
					p.color.y = chroma1;
					p.color.z = chroma2;
				}
			}

			points.insert(points.end(), buffer.begin(), buffer.end());
		}
		else {
			for (int i = 0; i < 8; ++i) {
				if (controlByte & 0x80) {
					int halfLength = node.edgeLength / 2;
					auto center = node.origin + halfLength;
					DecodeTreeNode nextNode;
					nextNode.origin = Vec3i32((i & 4 ? center.x : node.origin.x),
											  (i & 2 ? center.y : node.origin.y),
											  (i & 1 ? center.z : node.origin.z));
					nextNode.edgeLength = halfLength;
					nextNode.level = node.level - 1;
					q.push(nextNode);
				}
				controlByte <<= 1;
			}
		}

		q.pop();
	} while (!q.empty());

	return points;
}

std::string Slice::serialize() const {
	std::string result;
	uint8_t tzNum = _tzcnt_u32(edgeLength);  // ����߳���Ӧ��(1<<N)��λ����������8bit����
	int treeNum = encodedTree.size();
	int colorsNum = encodedColor.size();
	result.insert(result.size(), (char*)&origin, 12);
	result.insert(result.size(), (char*)&treeNum, 4);
	result.insert(result.size(), (char*)&colorsNum, 4);
	result += tzNum;
	result += clipDepth;
	result += isChromaSubsampling;
	result.insert(result.size(), (char*)encodedTree.data(), treeNum);
	result.insert(result.size(), (char*)encodedColor.data(), colorsNum);
	return result;
}

Slice Slice::parse(std::string_view view) {
	Vec3i32 origin;
	int treeNum, colorsNum;
	int index = 0;
	memcpy(&origin, &view[index], 12);
	index += 12;
	memcpy(&treeNum, &view[index], 4);
	index += 4;
	memcpy(&colorsNum, &view[index], 4);
	index += 4;
	uint8_t tzNum = view[index];
	int clipDepth = view[index + 1];
	bool isChromaSubsampling = view[index + 2];
	index += 3;

	Slice slice(origin, 1 << tzNum, clipDepth, isChromaSubsampling);
	slice.encodedTree.resize(treeNum);
	slice.encodedColor.resize(colorsNum);
	memcpy(slice.encodedTree.data(), &view[index], treeNum);
	index += treeNum;
	memcpy(slice.encodedColor.data(), &view[index], colorsNum);

	return slice;
}