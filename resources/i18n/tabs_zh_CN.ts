<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_CN" sourcelanguage="en_US">
<context>
    <name>Ripes::CacheConfigWidget</name>
    <message>
        <location filename="../../src/cachesim/cacheconfigwidget.ui" line="+34"/>
        <source>Cache configuration:</source>
        <translation>缓存配置：</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Preset:</source>
        <translation>预设：</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Write hit:</source>
        <translation>写命中：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>&lt;html&gt;&lt;head/&gt;&lt;body&gt;&lt;p&gt;2&lt;span style=&quot; vertical-align:super;&quot;&gt;N&lt;/span&gt; Sets:&lt;/p&gt;&lt;/body&gt;&lt;/html&gt;</source>
        <translation>&lt;html&gt;&lt;head/&gt;&lt;body&gt;&lt;p&gt;2&lt;span style=&quot; vertical-align:super;&quot;&gt;N&lt;/span&gt; 组数：&lt;/p&gt;&lt;/body&gt;&lt;/html&gt;</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>&lt;html&gt;&lt;head/&gt;&lt;body&gt;&lt;p&gt;2&lt;span style=&quot; vertical-align:super;&quot;&gt;N&lt;/span&gt; Words/Line:&lt;/p&gt;&lt;/body&gt;&lt;/html&gt;</source>
        <translation>&lt;html&gt;&lt;head/&gt;&lt;body&gt;&lt;p&gt;2&lt;span style=&quot; vertical-align:super;&quot;&gt;N&lt;/span&gt; 字/行：&lt;/p&gt;&lt;/body&gt;&lt;/html&gt;</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Write miss:</source>
        <translation>写缺失：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Ways (count):</source>
        <translation>路数（个数）：</translation>
    </message>
    <message>
        <location line="+23"/>
        <source>Replacement:</source>
        <translation>替换策略：</translation>
    </message>
    <message>
        <location filename="../../src/cachesim/cacheconfigwidget.cpp" line="+78"/>
        <source>Save the current cache configuration as a named preset</source>
        <translation>把当前缓存配置保存为命名预设</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Delete the selected cache preset</source>
        <translation>删除所选的缓存预设</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Store cache preset</source>
        <translation>保存缓存预设</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Preset name:</source>
        <translation>预设名称：</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>Are you sure you want to delete preset &apos;%1&apos;?</source>
        <translation>确定要删除预设“%1”吗？</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Delete cache preset</source>
        <translation>删除缓存预设</translation>
    </message>
    <message>
        <location line="+66"/>
        <source>direct-mapped</source>
        <translation>直接映射</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>fully associative</source>
        <translation>全相联</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>%1-way set associative</source>
        <translation>%1 路组相联</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Capacity: %1 sets x %2 ways x %3 words x %4 bytes = %5 bytes of data (%6). Address split: tag %7 bits | index %8 bits | word %9 bits | byte offset 2 bits.</source>
        <translation>容量：%1 组 x %2 路 x %3 字 x %4 字节 = %5 字节数据（%6）。地址划分：标记（tag）%7 位 | 索引（index）%8 位 | 字偏移 %9 位 | 字节偏移 2 位。</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Saved cache configurations. Selecting a preset sets all parameters below at once. Use the buttons to save the current configuration or delete the selected preset.</source>
        <translation>已保存的缓存配置。选择一个预设会一次性设置下面的全部参数。用右侧按钮保存当前配置或删除所选预设。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Number of sets (cache lines selected by the index bits), given as the exponent N: the cache has 2^N sets. Example: N = 3 gives 8 sets. Each additional bit doubles the cache size.</source>
        <translation>组数（由索引位选择的缓存行），以指数 N 表示：缓存共有 2^N 组。例如 N = 3 即 8 组。每增加 1 位，缓存容量翻倍。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Associativity: number of ways (blocks) in every set, as a plain count. 1 = direct-mapped: each address can live in exactly one place. More ways reduce conflict misses but need a replacement policy to pick the victim.</source>
        <translation>相联度：每组中的路（块）数，直接填写个数。1 = 直接映射：每个地址只能放在唯一位置。路数越多冲突缺失越少，但需要替换策略来选择被替换的路。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Line (block) size, given as the exponent N: each line holds 2^N words of 4 bytes. Larger lines exploit spatial locality (neighbouring addresses are loaded together) but cost more per miss.</source>
        <translation>行（块）大小，以指数 N 表示：每行包含 2^N 个 4 字节的字。行越大越能利用空间局部性（相邻地址一起载入），但每次缺失的代价更高。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Replacement policy: which way of a full set is evicted on a miss. LRU evicts the least recently used way (the LRU column shows the age, 0 = most recent); Random picks any way.</source>
        <translation>替换策略：组已满且发生缺失时替换哪一路。LRU 替换最近最少使用的路（LRU 列显示“年龄”，0 = 最近使用）；Random 随机选择一路。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Write-hit policy. Write-through: every store is also written to memory immediately. Write-back: the store only updates the cache and marks the line dirty (D column); memory is updated when the line is evicted.</source>
        <translation>写命中策略。写直达（Write-through）：每次写操作同时立即写入内存。写回（Write-back）：只更新缓存并把该行标记为脏（D 列）；该行被替换时才写回内存。</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Write-miss policy. Write allocate: the missing line is loaded into the cache, then written. No write allocate: the store goes directly to memory and the cache is not changed.</source>
        <translation>写缺失策略。写分配（Write allocate）：先把缺失的行载入缓存再写。非写分配（No write allocate）：直接写入内存，缓存不变。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Derived from the parameters above: total data capacity and how a 32-bit address is split into tag, index and word offset.</source>
        <translation>由上面的参数推导：总数据容量，以及 32 位地址如何划分为标记、索引和字偏移。</translation>
    </message>
    <message>
        <location filename="../../src/cachesim/cachesim.h" line="+342"/>
        <source>Random</source>
        <translation>随机（Random）</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>LRU</source>
        <translation>LRU</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Write allocate</source>
        <translation>写分配</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>No write allocate</source>
        <translation>非写分配</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Write-through</source>
        <translation>写直达</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Write-back</source>
        <translation>写回</translation>
    </message>
</context>
<context>
    <name>Ripes::CacheGraphic</name>
    <message>
        <location filename="../../src/cachesim/cachegraphic.cpp" line="+214"/>
        <source>Address: %1</source>
        <translation>地址：%1</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Dirty (modified, not yet written back)</source>
        <translation>脏（已修改，尚未写回）</translation>
    </message>
    <message>
        <location line="+217"/>
        <source>Access address</source>
        <translation>访问地址</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>Valid bit: 1 if this way holds data loaded from memory, 0 if empty</source>
        <translation>有效位：该路保存了从内存载入的数据时为 1，空时为 0</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Dirty bit: 1 if the line was written in the cache but not yet written back to memory (write-back policy)</source>
        <translation>脏位：该行在缓存中被写过但尚未写回内存时为 1（写回策略）</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Least Recently Used counter: the way with the highest value is evicted next</source>
        <translation>最近最少使用计数：数值最大的路下次被替换</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Tag</source>
        <translation>标记</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Word %1</source>
        <translation>字 %1</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Index</source>
        <translation>索引</translation>
    </message>
</context>
<context>
    <name>Ripes::CachePlotView</name>
    <message>
        <location filename="../../src/cachesim/cacheplotview.cpp" line="+205"/>
        <source>Cycle: %1 
Value: %2 </source>
        <translation>周期：%1 
数值：%2 </translation>
    </message>
</context>
<context>
    <name>Ripes::CachePlotWidget</name>
    <message>
        <location filename="../../src/cachesim/cacheplotwidget.ui" line="+36"/>
        <source>Plot configuration:</source>
        <translation>图表配置：</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Moving avg.</source>
        <translation>移动平均</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Denominator</source>
        <translation>分母</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Numerator</source>
        <translation>分子</translation>
    </message>
    <message>
        <location line="+13"/>
        <source> cyc.</source>
        <translation> 周期</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Show/hide the cumulative ratio plot: (numerator / denominator) x 100 %, accumulated from cycle 0</source>
        <translation>显示/隐藏累计比值曲线：(分子 / 分母) x 100 %，从第 0 周期开始累计</translation>
    </message>
    <message>
        <location line="+3"/>
        <location filename="../../src/cachesim/cacheplotwidget.cpp" line="+139"/>
        <location line="+113"/>
        <source>Ratio</source>
        <translation>比值</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>Statistics:</source>
        <translation>统计：</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Size (bits):</source>
        <translation>大小（位）：</translation>
    </message>
    <message>
        <location line="+60"/>
        <source>Writebacks:</source>
        <translation>写回次数：</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Hit rate:</source>
        <translation>命中率：</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Hits:</source>
        <translation>命中：</translation>
    </message>
    <message>
        <location line="+26"/>
        <source>Misses:</source>
        <translation>缺失：</translation>
    </message>
    <message>
        <location line="+82"/>
        <source>First cycle shown in the plot</source>
        <translation>图表显示的起始周期</translation>
    </message>
    <message>
        <location line="+16"/>
        <source>Drag the handles to choose the range of clock cycles shown in the plot</source>
        <translation>拖动滑块选择图表显示的时钟周期范围</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Last cycle shown in the plot</source>
        <translation>图表显示的结束周期</translation>
    </message>
    <message>
        <location filename="../../src/cachesim/cacheplotwidget.cpp" line="-214"/>
        <source>Cache Access Statistics</source>
        <translation>缓存访问统计</translation>
    </message>
    <message>
        <location line="+68"/>
        <source>Total: %1 bits</source>
        <translation>合计：%1 位</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Cache Size Breakdown</source>
        <translation>缓存大小明细</translation>
    </message>
    <message>
        <location line="+33"/>
        <location line="+125"/>
        <source>Moving average</source>
        <translation>移动平均</translation>
    </message>
    <message>
        <location line="-19"/>
        <source>Enable ratio crosshair</source>
        <translation>启用比值十字准线</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Enable moving average crosshair</source>
        <translation>启用移动平均十字准线</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Copy data to clipboard (tab separated, one row per cycle)</source>
        <translation>复制数据到剪贴板（制表符分隔，每周期一行）</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>Save plot to a PNG file</source>
        <translation>把图表保存为 PNG 文件</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Save file</source>
        <translation>保存文件</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Images (*.png)</source>
        <translation>图片 (*.png)</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>cycle</source>
        <translation>周期</translation>
    </message>
    <message>
        <location line="+108"/>
        <source>%1 / %2 (%)</source>
        <translation>%1 / %2 (%)</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Clock cycle</source>
        <translation>时钟周期</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>No accesses inside the plot window - see &quot;Max. cache plot cycles&quot; in Edit &gt; Settings &gt; Environment.</source>
        <translation>绘图窗口内没有访问记录——请查看 编辑 &gt; 设置 &gt; 环境 中的“缓存图最大周期数”。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>No cache accesses recorded yet - step or run a program to fill this plot.</source>
        <translation>尚未记录缓存访问——单步或运行程序后这里会出现曲线。</translation>
    </message>
    <message>
        <location line="+170"/>
        <source>Only cycles %1 - %2 are plotted; older cycles were dropped. To plot more, raise &quot;Max. cache plot cycles&quot; in Edit &gt; Settings &gt; Environment.</source>
        <translation>仅绘制了第 %1 - %2 周期；更早的周期已被丢弃。如需绘制更多，请增大 编辑 &gt; 设置 &gt; 环境 中的“缓存图最大周期数”。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Long runs keep only the most recent cycles in the plot, so that plotting stays fast. The ratio is still counted from cycle 0, and the statistics above cover the whole run. &quot;Max. cache plot cycles&quot; sets how many cycles are plotted, &quot;Max. cache access traces&quot; how many cache accesses are kept (both in Edit &gt; Settings &gt; Environment).</source>
        <translation>长时间运行时，图中只保留最近的周期，以保持绘图流畅。比值仍从第 0 周期开始累计，上方的统计数据覆盖整个运行过程。“缓存图最大周期数”决定绘制多少个周期，“最大缓存访问记录数”决定保留多少次缓存访问（两者都在 编辑 &gt; 设置 &gt; 环境 中）。</translation>
    </message>
    <message>
        <location line="+74"/>
        <source>Numerator of the plotted ratio. The plot shows the cumulative value numerator / denominator x 100 % over time; the default Hits / Access count is the hit rate.</source>
        <translation>所绘比值的分子。图表显示随时间累计的 分子 / 分母 x 100 %；默认的 命中 / 访问次数 即命中率。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Denominator of the plotted ratio (see Numerator).</source>
        <translation>所绘比值的分母（见“分子”）。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Also plot a moving average of the ratio over the last N cycles, which shows how the program behaves right now rather than since the start.</source>
        <translation>同时绘制比值在最近 N 个周期上的移动平均，反映程序“当前”的表现而不是从开始至今的累计。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Window of the moving average, in clock cycles.</source>
        <translation>移动平均的窗口大小，单位为时钟周期。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Total storage needed by this cache, in bits: data plus tag, valid, dirty and LRU bookkeeping bits.</source>
        <translation>该缓存所需的总存储（位）：数据位加上标记、有效、脏和 LRU 管理位。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Show how the cache size is split between data, tag and control bits</source>
        <translation>显示缓存大小在数据位、标记位和控制位之间的分配</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Hit rate = hits / (hits + misses), as a fraction between 0 and 1.</source>
        <translation>命中率 = 命中 / (命中 + 缺失)，取值 0 到 1。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Number of accesses whose data was found in the cache.</source>
        <translation>数据在缓存中找到的访问次数。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Number of accesses whose data was not in the cache and had to be fetched from the next level (memory).</source>
        <translation>数据不在缓存中、必须从下一级（内存）取来的访问次数。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Number of dirty lines written back to memory when they were evicted (write-back policy only).</source>
        <translation>被替换时写回内存的脏行数（仅写回策略）。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Cache statistics over time. X axis: clock cycle; Y axis: the selected ratio in percent. The legend below the plot names each line. Click the crosshair buttons to inspect values.</source>
        <translation>缓存统计随时间的变化。X 轴：时钟周期；Y 轴：所选比值（百分比）。图下方的图例标明每条曲线。点击十字准线按钮可查看数值。</translation>
    </message>
    <message>
        <location filename="../../src/cachesim/cacheplotwidget.h" line="+144"/>
        <source>Writes</source>
        <translation>写次数</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Reads</source>
        <translation>读次数</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Hits</source>
        <translation>命中次数</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Misses</source>
        <translation>缺失次数</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Writebacks</source>
        <translation>写回次数</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Access count</source>
        <translation>访问次数</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Was hit</source>
        <translation>本次命中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Was miss</source>
        <translation>本次缺失</translation>
    </message>
</context>
<context>
    <name>Ripes::CacheSim</name>
    <message>
        <location filename="../../src/cachesim/cachesim.cpp" line="+94"/>
        <source>Valid bits: %1</source>
        <translation>有效位：%1</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Dirty bits: %1</source>
        <translation>脏位：%1</translation>
    </message>
    <message>
        <location line="+7"/>
        <source>LRU bits: %1</source>
        <translation>LRU 位：%1</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Tag bits: %1</source>
        <translation>标记位：%1</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>Data bits: %1</source>
        <translation>数据位：%1</translation>
    </message>
</context>
<context>
    <name>Ripes::CacheTab</name>
    <message>
        <location filename="../../src/cachetab.cpp" line="+60"/>
        <source>Cache simulator</source>
        <translation>缓存模拟器</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Configure the L1 data and instruction caches on the left and watch every memory access in the diagram on the right: the address is split into tag / index / word, the indexed set is highlighted and the accessed word is marked as a hit or a miss. Statistics and the hit-rate plot update as the program runs.</source>
        <translation>在左侧配置 L1 数据缓存和指令缓存，在右侧图中观察每一次内存访问：地址被拆分为 标记 / 索引 / 字，被索引的组会高亮，被访问的字标为命中或缺失。统计数据和命中率曲线随程序运行更新。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Indexed set / word</source>
        <translation>被索引的组 / 字</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The set (row) selected by the index bits and the word (column) selected by the word-offset bits of the last access</source>
        <translation>最近一次访问中由索引位选中的组（行）和由字偏移位选中的字（列）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Hit</source>
        <translation>命中</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The accessed word was found in the cache</source>
        <translation>被访问的字在缓存中找到</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Miss</source>
        <translation>缺失</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>The accessed word was not in the cache and was loaded from the next level</source>
        <translation>被访问的字不在缓存中，已从下一级载入</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Dirty word</source>
        <translation>脏字</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Written in the cache but not yet in memory (write-back policy)</source>
        <translation>已写入缓存但尚未写入内存（写回策略）</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Cache diagram: one row per way of each set (Index column). V = valid bit, D = dirty bit, LRU = age for replacement, Tag = upper address bits stored with the line, Word N = cached data. Click a word to show its address in the Memory tab. Ctrl + mouse wheel zooms; middle-drag or Ctrl + drag pans.</source>
        <translation>缓存示意图：每组的每一路占一行（Index 列为组号）。V = 有效位，D = 脏位，LRU = 替换用的“年龄”，Tag = 与行一起保存的高位地址，Word N = 缓存的数据。点击某个字可在 Memory 标签页中显示其地址。Ctrl + 鼠标滚轮缩放；中键拖动或 Ctrl + 拖动平移。</translation>
    </message>
</context>
<context>
    <name>Ripes::CacheTabWidget</name>
    <message>
        <location filename="../../src/cachetabwidget.ui" line="+36"/>
        <source>L1 Data Cache</source>
        <translation>L1 数据缓存</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>L1 Instr. Cache</source>
        <translation>L1 指令缓存</translation>
    </message>
    <message>
        <location filename="../../src/cachetabwidget.cpp" line="+44"/>
        <source>Placeholder</source>
        <translation>占位</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>Level-1 data cache: serves the loads and stores executed by the program (lw/sw/...).</source>
        <translation>一级数据缓存：服务程序执行的读写指令（lw/sw/…）。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Level-1 instruction cache: serves the instruction fetches of the processor (one access per fetched instruction).</source>
        <translation>一级指令缓存：服务处理器的取指（每取一条指令访问一次）。</translation>
    </message>
    <message>
        <location line="+50"/>
        <source>L%1 Cache</source>
        <translation>L%1 缓存</translation>
    </message>
</context>
<context>
    <name>Ripes::CacheWidget</name>
    <message>
        <location filename="../../src/cachesim/cachewidget.cpp" line="+42"/>
        <source>Describes the most recent access to this cache: the address, whether it was a read or a write, whether the data was found in the cache (hit) or had to be fetched from the next level (miss), and where it is stored (set/line index, way and word).</source>
        <translation>描述对该缓存的最近一次访问：地址、读还是写、数据是否在缓存中找到（命中）还是需从下一级取（缺失），以及存放位置（组/行索引、路和字）。</translation>
    </message>
    <message>
        <location line="+20"/>
        <source>No cache accesses yet. Assemble a program and step the processor (or run it) to see each access marked as a hit or a miss.</source>
        <translation>尚无缓存访问。汇编程序后单步执行（或运行）处理器，即可看到每次访问被标为命中或缺失。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>%1 hits, %2 misses so far. Step the processor to see individual accesses highlighted in the diagram.</source>
        <translation>目前 %1 次命中，%2 次缺失。单步执行处理器可在图中看到每次访问的高亮。</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>HIT</source>
        <translation>命中</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>MISS</source>
        <translation>缺失</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Write</source>
        <translation>写</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Read</source>
        <translation>读</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Last access: %1 %2 &amp;rarr; %3 (set %4, way %5, word %6)</source>
        <translation>最近访问：%1 %2 &amp;rarr; %3（组 %4，路 %5，字 %6）</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>a dirty line was written back to memory</source>
        <translation>一个脏行被写回内存</translation>
    </message>
</context>
<context>
    <name>Ripes::IOClock</name>
    <message>
        <location filename="../../src/io/ioclock.cpp" line="+14"/>
        <source>Low 32 bits of the time in ns; reading it samples the clock and latches TIME_HI</source>
        <translation>时间（纳秒）的低 32 位；读取时采样时钟并锁存 TIME_HI</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>High 32 bits of the time sampled by the last TIME_LO read</source>
        <translation>最近一次读 TIME_LO 时采样到的时间的高 32 位</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Current host time in nanoseconds since 1970-01-01 00:00 UTC (the 64-bit value returned by TIME_HI:TIME_LO)</source>
        <translation>主机当前时间，自 1970-01-01 00:00 UTC 起的纳秒数（即 TIME_HI:TIME_LO 返回的 64 位值）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The same time as local date and clock time</source>
        <translation>同一时间的本地日期和时刻</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Epoch: %1 ns</source>
        <translation>纪元时间：%1 ns</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>A real-time clock exposing the host machine&apos;s wall-clock time as nanoseconds since the Unix epoch (a 64-bit value).</source>
        <translation>实时时钟：以自 Unix 纪元起的纳秒数（64 位值）提供主机的墙上时钟时间。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The value is split across two 32-bit registers:</source>
        <translation>该值分布在两个 32 位寄存器中：</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>  TIME_LO (offset 0): low 32 bits</source>
        <translation>  TIME_LO（偏移 0）：低 32 位</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>  TIME_HI (offset 4): high 32 bits</source>
        <translation>  TIME_HI（偏移 4）：高 32 位</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Reading TIME_LO samples the full 64-bit time, returns its low word, and latches the high word. Read TIME_LO first, then TIME_HI, to obtain a coherent 64-bit value without tearing. On 64-bit targets the whole value may be read with a single 8-byte access at offset 0.</source>
        <translation>读 TIME_LO 会采样完整的 64 位时间，返回低字并锁存高字。先读 TIME_LO 再读 TIME_HI，即可得到一致、不撕裂的 64 位值。在 64 位目标上，也可以在偏移 0 处用一次 8 字节访问读取整个值。</translation>
    </message>
</context>
<context>
    <name>Ripes::IODPad</name>
    <message>
        <location filename="../../src/io/iodpad.cpp" line="+27"/>
        <source>up</source>
        <translation>上</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>down</source>
        <translation>下</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>left</source>
        <translation>左</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>right</source>
        <translation>右</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>Button %1 (register %2): press with the mouse or the keyboard when the D-pad has focus</source>
        <translation>“%1”按钮（寄存器 %2）：用鼠标按下，或在方向键部件获得焦点时用键盘按下</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>1 if the %1 button is held or was pressed since the last read (reading clears the captured press)</source>
        <translation>“%1”按钮被按住或自上次读取以来被按过时为 1（读取会清除捕获的按键）</translation>
    </message>
    <message>
        <location line="+90"/>
        <source>Each button maps to a 32-bit register, with the least-significant bit indicating the state of the button.
</source>
        <translation>每个按钮对应一个 32 位寄存器，最低位表示按钮状态。
</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>If the D-pad window is in focus, the buttons may be pressed using the &quot;WASD&quot; or arrow keys of the keyboard.
</source>
        <translation>当方向键窗口获得焦点时，可用键盘的 &quot;WASD&quot; 或方向键按下按钮。
</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>A button register reads as 1 if the button is currently held, or if it was pressed at any point since the register was last read (edge/press capture). Reading a register clears this captured press, so brief taps between polls are not missed.</source>
        <translation>如果按钮当前被按住，或自上次读取该寄存器以来曾被按下（边沿/按键捕获），按钮寄存器读出为 1。读取寄存器会清除捕获的按键，因此两次轮询之间的短暂按键不会丢失。</translation>
    </message>
</context>
<context>
    <name>Ripes::IOLedMatrix</name>
    <message>
        <location filename="../../src/io/ioledmatrix.cpp" line="+18"/>
        <source>Height (LEDs)</source>
        <translation>高度（LED 个数）</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Width (LEDs)</source>
        <translation>宽度（LED 个数）</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>LED size (pixels)</source>
        <translation>LED 大小（像素）</translation>
    </message>
    <message>
        <location line="+18"/>
        <source>Each LED maps to a 24-bit register storing an RGB color value (0xRRGGBB), with B stored in the least significant byte.</source>
        <translation>每个 LED 对应一个 24 位寄存器，保存 RGB 颜色值（0xRRGGBB），B 位于最低字节。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>The byte offset of the LED at column x, row y is:</source>
        <translation>第 x 列、第 y 行 LED 的字节偏移为：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Hover over an LED to see its coordinates, offset and colour.</source>
        <translation>把鼠标悬停在 LED 上可查看其坐标、偏移和颜色。</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Colour of the LED at x = %1, y = %2 (0xRRGGBB)</source>
        <translation>位于 x = %1, y = %2 的 LED 的颜色（0xRRGGBB）</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>LED x = %1, y = %2
Offset: 0x%3
Colour: 0x%4</source>
        <translation>LED x = %1, y = %2
偏移：0x%3
颜色：0x%4</translation>
    </message>
</context>
<context>
    <name>Ripes::IOPeripheralTab</name>
    <message>
        <location filename="../../src/ioperipheraltab.ui" line="+32"/>
        <source>Parameters</source>
        <translation>参数</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Register map</source>
        <translation>寄存器映射</translation>
    </message>
    <message>
        <location line="+24"/>
        <source>Exported symbols</source>
        <translation>导出符号</translation>
    </message>
    <message>
        <location filename="../../src/ioperipheraltab.cpp" line="+26"/>
        <source>Registers of this device. Address = base address of the device + offset. Hover over a column header for its meaning.</source>
        <translation>该设备的寄存器。地址 = 设备基地址 + 偏移。把鼠标悬停在列标题上可查看含义。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>How this device works and how a program talks to it</source>
        <translation>该设备如何工作，以及程序如何与它交互</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Symbols generated for this device. Use them in assembly (e.g. li a0, NAME) or in C after #include &quot;ripes_system.h&quot;. They are updated automatically when parameters change.</source>
        <translation>为该设备生成的符号。可在汇编中使用（例如 li a0, NAME），或在 C 中 #include &quot;ripes_system.h&quot; 后使用。参数改变时会自动更新。</translation>
    </message>
    <message>
        <location line="+13"/>
        <source>Configuration of this device. Double-click a value to change it (not possible while the processor is running).</source>
        <translation>该设备的配置。双击数值可修改（处理器运行时不可修改）。</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Symbol prefix of this device: %1_ (the instance name &quot;%2&quot; in upper case, with spaces and &apos;-&apos; replaced by &apos;_&apos;). Example: %1_BASE is the base address.</source>
        <translation>该设备的符号前缀：%1_（实例名 &quot;%2&quot; 转为大写，空格和 &apos;-&apos; 替换为 &apos;_&apos;）。例如 %1_BASE 是基地址。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Exported symbols use the prefix %1_ (e.g. %1_BASE).</source>
        <translation>导出符号使用前缀 %1_（例如 %1_BASE）。</translation>
    </message>
</context>
<context>
    <name>Ripes::IOScreen</name>
    <message>
        <location filename="../../src/io/ioscreen.cpp" line="+15"/>
        <source>Width (pixels)</source>
        <translation>宽度（像素）</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Height (pixels)</source>
        <translation>高度（像素）</translation>
    </message>
    <message>
        <location line="+21"/>
        <source>A double-buffered raster screen. Each pixel is a 24-bit RGB value (0xRRGGBB, blue in the least significant byte).</source>
        <translation>双缓冲光栅屏幕。每个像素是一个 24 位 RGB 值（0xRRGGBB，蓝色在最低字节）。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>The framebuffer (the back buffer) starts at the peripheral base address. The byte offset of pixel (x, y) is:</source>
        <translation>帧缓冲（后台缓冲）从外设基地址开始。像素 (x, y) 的字节偏移为：</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Writing pixels does not update the display. Write any value to the PRESENT register to scan the back buffer onto the screen and flip to the other buffer for the next frame.</source>
        <translation>写像素不会立即更新显示。向 PRESENT 寄存器写任意值，才会把后台缓冲显示到屏幕上，并切换到另一个缓冲用于下一帧。</translation>
    </message>
    <message>
        <location line="+94"/>
        <source>Write any value to show the back buffer on the screen and swap buffers</source>
        <translation>写入任意值：把后台缓冲显示到屏幕上并交换缓冲</translation>
    </message>
</context>
<context>
    <name>Ripes::IOSwitches</name>
    <message>
        <location filename="../../src/io/ioswitches.cpp" line="+142"/>
        <source>Number of switches</source>
        <translation>开关数量</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Each switch maps to a bit in the memory-mapped register of the peripheral. Click a switch to toggle it.</source>
        <translation>每个开关对应外设内存映射寄存器中的一位。点击开关可切换其状态。</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>switch n = bit n (1 = on, 0 = off)</source>
        <translation>开关 n = 第 n 位（1 = 开，0 = 关）</translation>
    </message>
    <message>
        <location line="+12"/>
        <source>Switch %1: bit %1 of the register. Click to toggle.</source>
        <translation>开关 %1：寄存器的第 %1 位。点击切换。</translation>
    </message>
    <message>
        <location line="+32"/>
        <source>State of all switches: bit n = 1 when switch n is on</source>
        <translation>所有开关的状态：开关 n 打开时第 n 位为 1</translation>
    </message>
</context>
<context>
    <name>Ripes::IOTab</name>
    <message>
        <location filename="../../src/iotab.ui" line="+30"/>
        <source>Available devices</source>
        <translation>可用设备</translation>
    </message>
    <message>
        <location line="+31"/>
        <source>I/O exports (symbols for your program)</source>
        <translation>I/O 导出（供程序使用的符号）</translation>
    </message>
    <message>
        <location filename="../../src/iotab.cpp" line="+35"/>
        <source>I/O peripherals</source>
        <translation>I/O 外设</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Add memory-mapped devices (LEDs, switches, a screen, accelerators...) that your program controls by reading and writing their registers. Pick a device on the left and click &quot;Add selected device&quot; (or double-click it); its parameters, register map and exported symbols appear in the panel on the right. Base addresses are assigned in the order devices are added and are reassigned when a parameter changes a device&apos;s size - always use the exported symbols instead of hard-coded addresses.</source>
        <translation>添加内存映射设备（LED、开关、屏幕、加速器…），程序通过读写它们的寄存器来控制它们。在左侧选择设备并点击“添加所选设备”（或双击它）；其参数、寄存器映射和导出符号显示在右侧面板中。基地址按设备添加的顺序分配，当某个参数改变设备大小时会重新分配——请始终使用导出符号，不要写死地址。</translation>
    </message>
    <message>
        <location line="+39"/>
        <source>Device</source>
        <translation>设备</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>What it does</source>
        <translation>作用</translation>
    </message>
    <message>
        <location line="+15"/>
        <source>%1: %2
Double-click (or select and click Add) to add this device to the system.</source>
        <translation>%1：%2
双击（或选中后点击“添加”）把该设备加入系统。</translation>
    </message>
    <message>
        <location line="+36"/>
        <source>Add selected device</source>
        <translation>添加所选设备</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Create an instance of the selected device. It is placed at the next free address after the previous peripherals, so base addresses depend on the order in which devices are added and are reassigned when a parameter changes a device&apos;s size. Its base address and register symbols are listed under I/O exports.</source>
        <translation>创建所选设备的一个实例。它被放在已有外设之后的下一个空闲地址，因此基地址取决于设备的添加顺序，并在某个参数改变设备大小时重新分配。其基地址和寄存器符号列在“I/O 导出”中。</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>No peripherals yet.
Select a device in the list on the left and click &quot;Add selected device&quot; (or double-click it).</source>
        <translation>还没有外设。
在左侧列表中选择一个设备，然后点击“添加所选设备”（或双击它）。</translation>
    </message>
    <message>
        <location line="+55"/>
        <source>Exported symbols for all I/O devices.
These symbols may be referenced in Assembly
or C-language programs (C: #include &quot;ripes_system.h&quot;).</source>
        <translation>所有 I/O 设备的导出符号。
这些符号可以在汇编
或 C 语言程序中引用（C：#include &quot;ripes_system.h&quot;）。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Details of each added device: description, editable parameters, register map (offset, access, meaning) and the symbols exported for your program.</source>
        <translation>每个已添加设备的详细信息：说明、可编辑参数、寄存器映射（偏移、访问方式、含义）以及为程序导出的符号。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Live view of every added device. Close a device window to remove the device.</source>
        <translation>每个已添加设备的实时视图。关闭设备窗口即删除该设备。</translation>
    </message>
    <message>
        <location filename="../../src/io/ioregistry.h" line="+50"/>
        <source>LED Matrix</source>
        <translation>LED 矩阵</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Switches</source>
        <translation>开关</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>D-Pad</source>
        <translation>方向键</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Real-time Clock (RTC)</source>
        <translation>实时时钟（RTC）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Screen</source>
        <translation>屏幕</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>SIMT GPU</source>
        <translation>SIMT GPU</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Tensor Unit (NPU)</source>
        <translation>张量单元（NPU）</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Grid of RGB LEDs; write a 0xRRGGBB colour to an LED&apos;s word to light it.</source>
        <translation>RGB LED 阵列；向某个 LED 的字写入 0xRRGGBB 颜色即可点亮它。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Row of on/off switches you click; read one register, bit n = switch n.</source>
        <translation>一排可点击的开关；读一个寄存器，第 n 位 = 开关 n。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Up/down/left/right buttons (mouse or WASD/arrow keys); one register per button.</source>
        <translation>上/下/左/右按钮（鼠标或 WASD/方向键）；每个按钮一个寄存器。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Wall-clock time in nanoseconds since 1970, read as two 32-bit registers.</source>
        <translation>自 1970 年起的墙上时钟时间（纳秒），通过两个 32 位寄存器读取。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Double-buffered pixel display; write pixels, then write PRESENT to show the frame.</source>
        <translation>双缓冲像素显示器；先写像素，再写 PRESENT 显示该帧。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Teaching GPU that runs a kernel on many threads in lock-step warps (see the GPU chapter).</source>
        <translation>教学用 GPU：以锁步的 warp 在多个线程上运行 kernel（见 GPU 章节）。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Matrix-multiply accelerator with a descriptor queue, DMA and a systolic array (see the NPU chapter).</source>
        <translation>矩阵乘法加速器，带描述符队列、DMA 和脉动阵列（见 NPU 章节）。</translation>
    </message>
</context>
<context>
    <name>Ripes::InstructionModel</name>
    <message>
        <location filename="../../src/instructionmodel.cpp" line="+116"/>
        <source>BP</source>
        <translation>断点</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Breakpoints: click to stop execution before this instruction</source>
        <translation>断点：点击后在执行该指令之前暂停</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Addr</source>
        <translation>地址</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Instruction address</source>
        <translation>指令地址</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Stage</source>
        <translation>阶段</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Pipeline stage(s) currently executing the instruction</source>
        <translation>当前正在执行该指令的流水线阶段</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Instruction</source>
        <translation>指令</translation>
    </message>
</context>
<context>
    <name>Ripes::MemoryMapModel</name>
    <message>
        <location filename="../../src/io/memorymapmodel.cpp" line="+32"/>
        <source>%1 (empty)</source>
        <extracomment>Address range of a zero-sized memory map entry, e.g. an empty .data section. %1 is the start address.</extracomment>
        <translation>%1（空）</translation>
    </message>
    <message>
        <location line="+17"/>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Address range</source>
        <translation>地址范围</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Size (bytes)</source>
        <translation>大小（字节）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Description</source>
        <translation>说明</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Program section (e.g. .text, .data) or memory-mapped peripheral</source>
        <translation>程序段（如 .text、.data）或内存映射外设</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>First and last byte address occupied by the entry (hexadecimal, inclusive)</source>
        <translation>该条目占用的首、末字节地址（十六进制，含两端）</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Number of bytes occupied by the entry (decimal)</source>
        <translation>该条目占用的字节数（十进制）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>What this region of the address space is used for</source>
        <translation>这段地址空间的用途</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>Program code (instructions)</source>
        <translation>程序代码（指令）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Initialized global data</source>
        <translation>已初始化的全局数据</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Zero-initialized global data</source>
        <translation>零初始化的全局数据</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Read-only constants</source>
        <translation>只读常量</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Program section</source>
        <translation>程序段</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Memory-mapped peripheral: %1</source>
        <translation>内存映射外设：%1</translation>
    </message>
</context>
<context>
    <name>Ripes::MemoryModel</name>
    <message>
        <location filename="../../src/memorymodel.cpp" line="+68"/>
        <source>Address</source>
        <translation>地址</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Word</source>
        <translation>字</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Byte %1</source>
        <translation>字节 %1</translation>
    </message>
    <message>
        <location line="+6"/>
        <source>Address of the first byte of the row (hexadecimal). Rows are one word apart.</source>
        <translation>该行第一个字节的地址（十六进制）。相邻行相差一个字。</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Value of the whole word starting at this address (little-endian), shown in the selected display type</source>
        <translation>从该地址开始的整个字的值（小端序），按所选显示类型显示</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Byte at address + %1</source>
        <translation>地址 + %1 处的字节</translation>
    </message>
</context>
<context>
    <name>Ripes::MemoryTab</name>
    <message>
        <location filename="../../src/memorytab.ui" line="+36"/>
        <source>Memory viewer</source>
        <translation>内存查看器</translation>
    </message>
    <message>
        <location line="+29"/>
        <source>Memory map</source>
        <translation>内存映射</translation>
    </message>
    <message>
        <location filename="../../src/memorytab.cpp" line="+22"/>
        <source>Memory</source>
        <translation>内存</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Shows the simulated memory around a chosen address: each row is one word, with its address, the whole word and its individual bytes (byte 0 = lowest address, little-endian). Scroll with the mouse wheel or jump with &quot;Go to&quot;. The memory map on the right lists where the program sections and peripherals live.</source>
        <translation>显示所选地址附近的模拟内存：每行一个字，包括地址、整个字的值及其各个字节（字节 0 = 最低地址，小端序）。用鼠标滚轮滚动，或用“转到”跳转。右侧的内存映射列出程序段和外设所在的位置。</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Memory map: the address ranges used by the program sections (.text, .data, ...) and by the I/O peripherals. Empty until a program is loaded or a peripheral is added.</source>
        <translation>内存映射：程序段（.text、.data…）和 I/O 外设所占用的地址范围。载入程序或添加外设之前为空。</translation>
    </message>
</context>
<context>
    <name>Ripes::MemoryViewerWidget</name>
    <message>
        <location filename="../../src/memoryviewerwidget.cpp" line="+53"/>
        <source>Go to register:</source>
        <translation>转到寄存器：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Center the view on the address currently stored in a register (e.g. sp for the stack, gp for global data).</source>
        <translation>以某个寄存器当前保存的地址为中心显示（例如 sp 指向栈，gp 指向全局数据）。</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Go to section:</source>
        <translation>转到段：</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Center the view on the start of a program section (.text = code, .data = global variables) or on an address you type in (&quot;Address...&quot;).</source>
        <translation>以某个程序段的起始地址为中心显示（.text = 代码，.data = 全局变量），或以你输入的地址为中心（“Address...”）。</translation>
    </message>
    <message>
        <location line="+11"/>
        <source>X = never written (no value yet), - = outside the address space; greyed rows are uninitialized.</source>
        <translation>X = 从未写入（尚无值），- = 超出地址空间；灰色行表示未初始化。</translation>
    </message>
    <message>
        <location line="+49"/>
        <source>Memory contents, one word per row (addresses increase upwards). Scroll with the mouse wheel; hover over a column header for its meaning.</source>
        <translation>内存内容，每行一个字（地址向上递增）。用鼠标滚轮滚动；把鼠标悬停在列标题上可查看含义。</translation>
    </message>
</context>
<context>
    <name>Ripes::PeriphParamModel</name>
    <message>
        <location filename="../../src/io/periphparammodel.cpp" line="+62"/>
        <source>Parameter</source>
        <translation>参数</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Value</source>
        <translation>值</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Configuration parameter of this peripheral</source>
        <translation>该外设的配置参数</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Double-click to edit. Changing a parameter may change the peripheral&apos;s size and therefore the addresses of the peripherals after it; re-assemble your program afterwards.</source>
        <translation>双击编辑。修改参数可能改变外设大小，从而改变其后各外设的地址；修改后请重新汇编程序。</translation>
    </message>
    <message>
        <location line="+48"/>
        <source>Double-click to edit (range %1 - %2)</source>
        <translation>双击编辑（范围 %1 - %2）</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Double-click to edit</source>
        <translation>双击编辑</translation>
    </message>
</context>
<context>
    <name>Ripes::PipelineDiagramModel</name>
    <message>
        <location filename="../../src/pipelinediagrammodel.cpp" line="+177"/>
        <source>Cycle %1: %2 is in stage %3</source>
        <translation>周期 %1：%2 处于 %3 阶段</translation>
    </message>
    <message>
        <location line="+5"/>
        <source>&quot;-&quot; = the instruction stayed in the same stage as in the previous cycle (stall).</source>
        <translation>&quot;-&quot; = 该指令停留在与上一周期相同的阶段（停顿）。</translation>
    </message>
</context>
<context>
    <name>Ripes::PipelineDiagramWidget</name>
    <message>
        <location filename="../../src/pipelinediagramwidget.ui" line="+14"/>
        <location filename="../../src/pipelinediagramwidget.cpp" line="+31"/>
        <source>Pipeline diagram</source>
        <translation>流水线图</translation>
    </message>
    <message>
        <location line="+14"/>
        <source>Copy table to clipboard (tab separated)</source>
        <translation>复制表格到剪贴板（制表符分隔）</translation>
    </message>
    <message>
        <location line="+35"/>
        <source>&lt;html&gt;&lt;head/&gt;&lt;body&gt;&lt;p&gt;&lt;span style=&quot; font-size:10pt; font-style:italic;&quot;&gt;# of recorded cycles can be changed in settings&lt;/span&gt;&lt;/p&gt;&lt;/body&gt;&lt;/html&gt;</source>
        <translation>&lt;html&gt;&lt;head/&gt;&lt;body&gt;&lt;p&gt;&lt;span style=&quot; font-size:10pt; font-style:italic;&quot;&gt;记录的周期数可在设置中修改&lt;/span&gt;&lt;/p&gt;&lt;/body&gt;&lt;/html&gt;</translation>
    </message>
    <message>
        <location filename="../../src/pipelinediagramwidget.cpp" line="+1"/>
        <source>One row per instruction of the program, one column per clock cycle. A cell names the pipeline stage the instruction occupies in that cycle; &quot;-&quot; means it stayed in the same stage (stall). Read a row left to right to follow one instruction through the pipeline.</source>
        <translation>每行对应程序中的一条指令，每列对应一个时钟周期。单元格写出该指令在该周期所处的流水线阶段；&quot;-&quot; 表示它停留在同一阶段（停顿）。从左到右读一行，即可跟踪一条指令流经流水线的过程。</translation>
    </message>
    <message>
        <location line="+19"/>
        <source>Instruction is in the %1 stage</source>
        <translation>指令处于 %1 阶段</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>- (stall)</source>
        <translation>-（停顿）</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Instruction stayed in the same stage as in the previous cycle</source>
        <translation>指令停留在与上一周期相同的阶段</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Notes: &quot;-&quot; = stalled (same stage as in the previous cycle). An entry such as &quot;IF/WB&quot; means the same instruction address is in two stages in that cycle (e.g. a loop body fetched again). Instructions that are flushed (e.g. after a taken branch) simply disappear from their row.</source>
        <translation>说明：&quot;-&quot; = 停顿（与上一周期处于同一阶段）。形如 &quot;IF/WB&quot; 的条目表示同一指令地址在该周期同时处于两个阶段（例如循环体被再次取指）。被冲刷（flush）的指令（例如分支跳转之后）会直接从其所在行中消失。</translation>
    </message>
    <message>
        <location line="+10"/>
        <source>The number of recorded cycles is set in Edit &gt; Settings &gt; Environment &gt; &quot;Max. pipeline diagram cycles&quot;. Cycles beyond this limit are not recorded.</source>
        <translation>记录的周期数在 Edit &gt; Settings &gt; Environment &gt; &quot;Max. pipeline diagram cycles&quot;（编辑 &gt; 设置 &gt; 环境 &gt; 流水线图最大周期数）中设置。超过此限制的周期不会被记录。</translation>
    </message>
    <message>
        <location line="+6"/>
        <source># of recorded cycles: Edit &gt; Settings &gt; Environment &gt; &quot;Max. pipeline diagram cycles&quot;</source>
        <translation>记录的周期数：Edit &gt; Settings &gt; Environment &gt; &quot;Max. pipeline diagram cycles&quot;（编辑 &gt; 设置 &gt; 环境）</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Hover over a cell to see the cycle, instruction and stage</source>
        <translation>把鼠标悬停在单元格上可查看周期、指令和阶段</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>No cycles recorded yet - step or run the processor, then reopen this diagram. The number of recorded cycles can be changed in the settings.</source>
        <translation>尚未记录任何周期——请单步或运行处理器后重新打开此图。记录的周期数可在设置中修改。</translation>
    </message>
</context>
<context>
    <name>Ripes::ProgramViewer</name>
    <message>
        <location filename="../../src/programviewer.cpp" line="+339"/>
        <source>Toggle breakpoint</source>
        <translation>切换断点</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Remove all breakpoints</source>
        <translation>删除全部断点</translation>
    </message>
</context>
<context>
    <name>Ripes::RadixSelectorWidget</name>
    <message>
        <location filename="../../src/radixselectorwidget.ui" line="+34"/>
        <source>Display type:</source>
        <translation>显示类型：</translation>
    </message>
    <message>
        <location filename="../../src/radix.h" line="+17"/>
        <source>Hex</source>
        <translation>十六进制</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Binary</source>
        <translation>二进制</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Unsigned</source>
        <translation>无符号十进制</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Signed</source>
        <translation>有符号十进制</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>ASCII</source>
        <translation>ASCII</translation>
    </message>
    <message>
        <location line="+0"/>
        <source>Float</source>
        <translation>浮点</translation>
    </message>
    <message>
        <location filename="../../src/radixselectorwidget.cpp" line="+36"/>
        <source>How values are displayed: Hex (0x...), Binary (0b...), Unsigned or Signed decimal, ASCII characters or IEEE-754 Float. Only the display changes, not the stored value.</source>
        <translation>数值的显示方式：十六进制 (0x...)、二进制 (0b...)、无符号或有符号十进制、ASCII 字符或 IEEE-754 浮点。只改变显示，不改变存储的值。</translation>
    </message>
</context>
<context>
    <name>Ripes::RegisterInitializationWidget</name>
    <message>
        <location filename="../../src/registerinitializationwidget.ui" line="+40"/>
        <source>Register initialization</source>
        <translation>寄存器初始化</translation>
    </message>
    <message>
        <location filename="../../src/registerinitializationwidget.cpp" line="+77"/>
        <source>Add a register that is set to a given value when the processor is reset</source>
        <translation>添加一个在处理器复位时被设为指定值的寄存器</translation>
    </message>
    <message>
        <location line="+112"/>
        <source>Remove this register initialization</source>
        <translation>删除此寄存器初始化</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Initial value (hexadecimal) loaded on reset</source>
        <translation>复位时载入的初始值（十六进制）</translation>
    </message>
</context>
<context>
    <name>Ripes::RegisterMapModel</name>
    <message>
        <location filename="../../src/io/registermapmodel.cpp" line="+41"/>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Offset</source>
        <translation>偏移</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Access</source>
        <translation>访问</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Bits</source>
        <translation>位数</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Description</source>
        <translation>说明</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Register name. Exported registers can be used in programs through the symbols listed under Exported symbols (address and offset of each register).</source>
        <translation>寄存器名称。导出的寄存器可以通过“导出符号”中列出的符号（每个寄存器的地址和偏移）在程序中使用。</translation>
    </message>
    <message>
        <location line="+4"/>
        <source>Byte offset of the register from the peripheral&apos;s base address (hexadecimal)</source>
        <translation>寄存器相对外设基地址的字节偏移（十六进制）</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>R = read-only, W = write-only, R/W = read and write</source>
        <translation>R = 只读，W = 只写，R/W = 可读可写</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Number of meaningful bits in the register</source>
        <translation>寄存器中有效的位数</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>What the register does</source>
        <translation>寄存器的作用</translation>
    </message>
    <message>
        <location line="+42"/>
        <source>R</source>
        <translation>R</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>W</source>
        <translation>W</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>R/W</source>
        <translation>R/W</translation>
    </message>
</context>
<context>
    <name>Ripes::RegisterModel</name>
    <message>
        <location filename="../../src/registermodel.cpp" line="+75"/>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Alias</source>
        <translation>别名</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Value</source>
        <translation>值</translation>
    </message>
    <message>
        <location line="+8"/>
        <source>Architectural register name (x0-x31, f0-f31, ...)</source>
        <translation>体系结构寄存器名（x0-x31、f0-f31…）</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>ABI name used in assembly by convention (zero, ra, sp, a0, t0, ...)</source>
        <translation>汇编中按约定使用的 ABI 名称（zero、ra、sp、a0、t0…）</translation>
    </message>
    <message>
        <location line="+3"/>
        <source>Current value in the selected display type. Double-click to edit; the most recently written register is highlighted.</source>
        <translation>当前值，按所选显示类型显示。双击可编辑；最近被写入的寄存器会高亮。</translation>
    </message>
</context>
<context>
    <name>Ripes::RegisterWidget</name>
    <message>
        <location filename="../../src/registerwidget.cpp" line="+48"/>
        <source>Register file. The most recently written register is highlighted (gold). Double-click a value to change it; right-click for copy / go to address.</source>
        <translation>寄存器堆。最近被写入的寄存器以金色高亮。双击数值可修改；右键可复制或转到地址。</translation>
    </message>
    <message>
        <location line="+22"/>
        <source>Copy register value</source>
        <translation>复制寄存器值</translation>
    </message>
    <message>
        <location line="+7"/>
        <location line="+2"/>
        <source>Set register value</source>
        <translation>设置寄存器值</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Register value</source>
        <translation>寄存器值</translation>
    </message>
    <message>
        <location line="+2"/>
        <source>Set</source>
        <translation>设置</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <location line="+9"/>
        <source>Go to address</source>
        <translation>转到地址</translation>
    </message>
    <message>
        <location line="+1"/>
        <source>Make all memory viewers move to the address contained in the selected register</source>
        <translation>让所有内存查看器跳到所选寄存器中保存的地址</translation>
    </message>
</context>
<context>
    <name>Ripes::TabHeader</name>
    <message>
        <location filename="../../src/tabheader.h" line="+120"/>
        <source>Legend:</source>
        <translation>图例：</translation>
    </message>
    <message>
        <location line="+43"/>
        <source>Open the user guide chapter that explains this view (%1)</source>
        <translation>打开讲解此视图的用户手册章节（%1）</translation>
    </message>
</context>
</TS>
