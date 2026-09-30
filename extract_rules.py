import pymupdf

doc = pymupdf.open(r'C:\Users\lenovo\Desktop\powerrune24-master\rules2026.pdf')

# 提取第101-106页（索引100-105）
for page_num in range(100, 107):
    page = doc[page_num]
    text = page.get_text('text', sort=True)
    print(f"\n{'='*60}")
    print(f"第 {page_num+1} 页")
    print(f"{'='*60}")
    print(text)
