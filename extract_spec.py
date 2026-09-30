import pymupdf

doc = pymupdf.open(r'C:\Users\lenovo\Desktop\RoboMaster\powerrune26-master\spec2026.pdf')

for page_num in [19, 41, 52, 55, 70, 76]:  # 0-indexed
    page = doc[page_num]
    text = page.get_text('text', sort=True)
    print(f"\n{'='*60}")
    print(f"第 {page_num+1} 页")
    print(f"{'='*60}")
    print(text[:3000])
