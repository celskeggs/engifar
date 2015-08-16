#!/usr/bin/env python3

import sys

if len(sys.argv) != 3:
	print("Usage: %s <FROM> <TO>" % sys.argv[0], file=sys.stderr)
	sys.exit(1)

ad = b''
with open(sys.argv[1], "rb") as af:
	while True:
		cad = af.read(8192)
		if not cad: break
		ad += cad
bd = b''
with open(sys.argv[2], "rb") as bf:
	while True:
		cbd = bf.read(8192)
		if not cbd: break
		bd += cbd
def trim_ends():
	si = 0
	while si < len(ad) and si < len(bd) and ad[si] == bd[si]:
		si += 1
	ei = 0
	while ei < len(ad) and ei < len(bd) and ad[-ei-1] == bd[-ei-1]:
		ei += 1
	return si & ~15, ei & ~15
start_i, end_i = trim_ends()
print("Files are %.6x/%.6x, changed in %.6x-%.6x/%.6x-%.6x" % (len(ad), len(bd), start_i, len(ad) - end_i - 1, start_i, len(bd) - end_i - 1))

# X in table is A, Y in table is B.
width = len(ad) - end_i - start_i + 1
height = len(bd) - end_i - start_i + 1
sources = bytearray(width * height) # [x + y * width]

L, T, DM, DUM = 1, 2, 4, 8

#sources[0] = None
for y in range(1, height):
	sources[y * width] = T
for x in range(1, width):
	sources[x] = L

print("Size:", width, height)

# fill in table
lastrow = list(reversed(range(1-width, 1)))
#print(">", *lastrow)
row = [None] * (width)
for y in range(1, height):
	ytw = y * width
	bd_y = bd[y + start_i]
	row[0] = best = -y
	for x in range(1, width): # this loop is the slow part
		leftval = best
		topval = lastrow[x]
		match = bd_y == ad[x + start_i]
		diagval = lastrow[x - 1] + (2 if match else 0)
		if leftval <= diagval and topval <= diagval:
			sources[x + ytw] = DM if match else DUM
			best = diagval
		elif leftval <= topval:
			sources[x + ytw] = T
			best = topval
		else:
			sources[x + ytw] = L
			best = leftval
		best -= 1
		row[x] = best
	#print(">", *row)
	lastrow, row = row, lastrow
	if height > 1000 and y % (height // 100) == 0:
		print("%s%% done" % (100 * y // height))

print("Trace:")
tr_x, tr_y = width - 1, height - 1
def gen_both():
	global tr_x, tr_y
	if tr_x <= 0 and tr_y <= 0:
		if tr_x == 0 and tr_y == 0:
			tr_x = tr_y = -1
			return start_i, " ", start_i, " "
		return None
	else:
		source = sources[tr_x + tr_y * width]
		assert source != 0 and source != None, "Bad: %d, %d" % (tr_x, tr_y)
	if source & L:
		tr_x -= 1
		return tr_x + 1 + start_i, "-", None, " "
	elif source & T:
		tr_y -= 1
		return None, " ", tr_y + 1 + start_i, "+"
	elif source & DM:
		tr_x -= 1
		tr_y -= 1
		return tr_x + 1 + start_i, " ", tr_y + 1 + start_i, " "
	elif source & DUM:
		tr_x -= 1
		tr_y -= 1
		return tr_x + 1 + start_i, "=", tr_y + 1 + start_i, "="
	else:
		assert False
tmp_buf = []
while True:
	out = gen_both()
	if out == None: break
	tmp_buf.append(out)
tmp_buf.reverse()
for i in range(0, len(tmp_buf), 16):
	chunk = tmp_buf[i:i+16]
	new_begin_a = None
	new_begin_b = None
	for aa, _1, ab, _2 in chunk:
		if aa != None:
			new_begin_a = aa
			if new_begin_b != None:
				break
		if ab != None:
			new_begin_b = ab
			if new_begin_a != None:
				break
	if len(chunk) < 16:
		st_a = st_b = None
		for i in range(len(chunk) - 1, -1, -1):
			if st_a == None:
				st_a = chunk[i][0]
			if st_b == None:
				st_b = chunk[i][2]
			if st_a != None and st_b != None:
				break
		while len(chunk) < 16:
			av = bv = "??"
			if st_a != None and st_a < len(ad):
				av = ad[st_a]
				st_a += 1
			if st_b != None and st_b < len(bd):
				bv = bd[st_b]
				st_b += 1
			chunk.append((av, " ", bv, " "))
	print(	("0x%.6x: " % (new_begin_a) if new_begin_a != None else "??????") + " ".join(modc + ((("%.2x" % (ad[k])) if k < len(ad) else "  ") if k != None else "--") + modc for k, modc, _1, _2 in chunk),\
		("0x%.6x: " % (new_begin_b) if new_begin_b != None else "??????") + " ".join(modc + ((("%.2x" % (bd[k])) if k < len(bd) else "  ") if k != None else "--") + modc for _1, _2, k, modc in chunk), "[" + ("".join(chr(ad[k]) if k and 32 <= ad[k] <= 126 else "." for k, _1, _2, _3 in chunk)) + "]", "[" + ("".join(chr(bd[k]) if k and 32 <= bd[k] <= 126 else "." for _1, _2, k, _3 in chunk)) + "]")

